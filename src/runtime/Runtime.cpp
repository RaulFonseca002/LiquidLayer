#include "liquid/Runtime.hpp"
#include "RuntimeInternals.hpp"

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <exception>
#include <stdexcept>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace liquid {

Runtime::Runtime() = default;

Runtime::Runtime(FrameNumber initialFrame)
    : currentFrame(initialFrame) {
}

Runtime::Runtime(liquid::RuntimeOptions options)
    : effectsState(
          std::make_unique<liquid::detail::RuntimeEffectsState>(std::move(options))) {
    ownedWorld.enable_script_evidence();
}

Runtime::~Runtime() = default;

void Runtime::ensure_owner_thread() const {
    if (std::this_thread::get_id() != ownerThread)
        throw std::logic_error("runtime used from a non-owner thread");
}

World& Runtime::world() {
    ensure_owner_thread();
    return ownedWorld;
}

const World& Runtime::world() const {
    ensure_owner_thread();
    return ownedWorld;
}

FrameNumber Runtime::frame() const {
    ensure_owner_thread();
    return currentFrame;
}

bool Runtime::faulted() const {
    ensure_owner_thread();
    return faultedState;
}

FrameLog Runtime::run_frame(
    IntentTime now,
    std::map<ComponentTypeId, std::map<ComponentName, ComponentSlotId>> resolutions
) {
    ensure_owner_thread();
    validate_frame_start(now, resolutions);
    frameInProgress = true;

    try {
        latestFrameLog = execute_frame_phases(now, resolutions);
    } catch (...) {
        frameInProgress = false;
        faultedState = true;
        throw;
    }

    frameInProgress = false;
    ++currentFrame;
    return latestFrameLog;
}

void Runtime::validate_frame_start(
    IntentTime now,
    const std::map<ComponentTypeId,
        std::map<ComponentName, ComponentSlotId>>& resolutions
) const {
    if (currentFrame == std::numeric_limits<FrameNumber>::max())
        throw std::overflow_error("frame number exhausted");
    if (faultedState)
        throw std::logic_error("runtime is faulted after an incomplete frame");

    if (frameInProgress)
        throw std::logic_error("runtime frame execution is not reentrant");

    if (currentFrame > 0 && now < latestFrameLog.now)
        throw std::invalid_argument("runtime time cannot move backwards");

    for (const auto& [type, components] : resolutions) {
        if (!ownedWorld.resolution_request_is_current(type, components))
            throw std::invalid_argument("intent resolution request contains a stale component target");
    }
}

FrameLog Runtime::execute_frame_phases(
    IntentTime now,
    const std::map<ComponentTypeId,
        std::map<ComponentName, ComponentSlotId>>& resolutions
) {
    FrameLog log;
    log.frame = currentFrame;
    log.now = now;
    std::size_t completedInPhase = 0;

    try {
        log.phases.push_back("begin_frame");

        log.phases.push_back("expire_intents");
        log.expired_intents = ownedWorld.destroy_expired_intents(now);

        log.phases.push_back("run_input_systems");
        ownedWorld.run_systems(
            currentFrame, now, SystemPhase::Input,
            &completedInPhase, &log.failure_system);
        log.systems_run += completedInPhase;
        completedInPhase = 0;

        log.phases.push_back("run_behavior_systems");
        ownedWorld.run_systems(
            currentFrame, now, SystemPhase::Behavior,
            &completedInPhase, &log.failure_system);
        log.systems_run += completedInPhase;
        completedInPhase = 0;

        log.phases.push_back("run_decision_systems");
        ownedWorld.run_systems(
            currentFrame, now, SystemPhase::Decision,
            &completedInPhase, &log.failure_system);
        log.systems_run += completedInPhase;
        completedInPhase = 0;

        log.phases.push_back("resolve_intents");
        log.expired_intents += ownedWorld.destroy_expired_intents(now);
        auto resolvedTargets = ownedWorld.resolution_targets();
        for (const auto& [type, components] : resolutions) {
            auto& targets = resolvedTargets[type];
            for (const auto& [name, slot] : components)
                targets.insert_or_assign(name, slot);
        }
        log.resolution_requests = resolvedTargets.size();

        for (const auto& [type, components] : resolvedTargets) {
            std::map<ComponentName, IntentId> selected = ownedWorld.resolve_intents(type, components, now);
            log.selected_intents += selected.size();
            log.intent_selections.emplace(type, std::move(selected));
        }

        log.phases.push_back("end_frame");
        log.completed = true;
    } catch (const std::exception& error) {
        log.systems_run += completedInPhase;
        log.failure_phase = log.phases.empty() ? "begin_frame" : log.phases.back();
        log.failure_message = error.what();
        latestFrameLog = log;
        throw;
    } catch (...) {
        log.systems_run += completedInPhase;
        log.failure_phase = log.phases.empty() ? "begin_frame" : log.phases.back();
        log.failure_message = "unknown exception";
        latestFrameLog = log;
        throw;
    }
    return log;
}

liquid::FrameResult Runtime::run_frame(liquid::FrameInput input) {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    validate_frame_start(input.now, input.resolutions);
    effectsState->validate_effects(input.resolvedEffects);
    frameInProgress = true;

    liquid::FrameResult result;
    const FrameNumber frameBeingRun = currentFrame;
    bool worldEvidenceAttempted = false;
    try {
        effectsState->append(liquid::EventType::FrameStarted,
            liquid::detail::event_payload({
                {"frame", liquid::Value{currentFrame}},
                {"now", liquid::Value{input.now}}
            }));
        effectsState->process_feedback(
            result.reports, result.observations);
        project_authoritative_reports(result.reports);
        project_authoritative_observations(result.observations);
        result.frame = execute_frame_phases(input.now, input.resolutions);
        std::vector<ResolvedEffect> resolvedEffects =
            apply_selections(result.frame);
        resolvedEffects.insert(
            resolvedEffects.end(),
            input.resolvedEffects.begin(), input.resolvedEffects.end());
        effectsState->validate_effects(resolvedEffects);
        for (const auto& [type, selections] : result.frame.intent_selections) {
            for (const auto& [name, id] : selections) {
                effectsState->append(liquid::EventType::ResolutionSelected,
                    liquid::detail::event_payload({
                        {"key", liquid::Value{
                            std::to_string(type) + ":" + name}},
                        {"value", liquid::Value{effectsState->session_id().value}},
                        {"type", liquid::Value{static_cast<std::uint64_t>(type)}},
                        {"name", liquid::Value{name}},
                        {"intent_world", liquid::Value{effectsState->session_id().value}},
                        {"intent_slot", liquid::Value{
                            static_cast<std::uint64_t>(id.slot)}},
                        {"intent_generation", liquid::Value{
                            static_cast<std::uint64_t>(id.generation)}}
                    }));
            }
        }
        for (const auto& effect : resolvedEffects) {
            auto command = effectsState->issue(effect, input.now, result.reports);
            if (command)
                result.commands.push_back(std::move(*command));
        }
        effectsState->retry_due(input.now, result.reports);
        project_authoritative_reports(result.reports);
        worldEvidenceAttempted = true;
        record_world_evidence();
        effectsState->prune_terminal_history();
        effectsState->append(liquid::EventType::FrameCompleted,
            liquid::detail::event_payload({
                {"frame", liquid::Value{result.frame.frame}},
                {"now", liquid::Value{input.now}}
            }));
        effectsState->flush();
    } catch (...) {
        const std::exception_ptr failure = std::current_exception();
        faultedState = true;
        if (!result.frame.completed && !latestFrameLog.completed &&
            latestFrameLog.frame == frameBeingRun) {
            result.frame = latestFrameLog;
        }
        result.frame.completed = false;
        if (result.frame.failure_phase.empty()) {
            result.frame.frame = frameBeingRun;
            result.frame.now = input.now;
            result.frame.failure_phase = "effects_and_persistence";
            try {
                std::rethrow_exception(failure);
            } catch (const std::exception& error) {
                result.frame.failure_message = error.what();
            } catch (...) {
                result.frame.failure_message = "unknown failure";
            }
        }
        try {
            if (!worldEvidenceAttempted)
                record_world_evidence();
            effectsState->append(liquid::EventType::FrameFailed,
                liquid::detail::event_payload({
                    {"frame", liquid::Value{frameBeingRun}},
                    {"now", liquid::Value{input.now}},
                    {"phase", liquid::Value{result.frame.failure_phase}},
                    {"message", liquid::Value{result.frame.failure_message}},
                    {"system", liquid::Value{result.frame.failure_system}},
                    {"expired_intents", liquid::Value{
                        static_cast<std::uint64_t>(result.frame.expired_intents)}},
                    {"selected_intents", liquid::Value{
                        static_cast<std::uint64_t>(result.frame.selected_intents)}},
                    {"systems_run", liquid::Value{
                        static_cast<std::uint64_t>(result.frame.systems_run)}}
                }));
            effectsState->flush();
        } catch (...) {
        }
        frameInProgress = false;
        throw;
    }
    latestFrameLog = result.frame;
    frameInProgress = false;
    ++currentFrame;
    return result;
}

void Runtime::register_adapter(std::shared_ptr<liquid::EffectAdapter> adapter) {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    effectsState->register_adapter(std::move(adapter));
}

liquid::FeedbackSender Runtime::feedback_sender() const {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    return effectsState->feedback_sender();
}

std::optional<liquid::Value> Runtime::observed_state(
    const liquid::AdapterRoute& route,
    const liquid::EffectTarget& target
) const {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    return effectsState->observed_state(route, target);
}

std::optional<liquid::CommandStatus> Runtime::command_status(
    liquid::CommandId commandId
) const {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    return effectsState->command_status(commandId);
}

void Runtime::reconcile_indeterminate(
    const liquid::AdapterRoute& route,
    const liquid::EffectTarget& target,
    liquid::Value observedValue
) {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    effectsState->reconcile_indeterminate(
        route, target, std::move(observedValue));
}

RecordId Runtime::checkpoint() {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    if (frameInProgress)
        throw std::logic_error("runtime checkpoint is unavailable during a frame");
    record_world_evidence();
    return effectsState->checkpoint();
}

const FrameLog& Runtime::last_frame_log() const {
    ensure_owner_thread();
    return latestFrameLog;
}

}
