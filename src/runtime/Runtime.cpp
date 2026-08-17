#include "liquid/Runtime.hpp"
#include "RuntimeInternals.hpp"
#include "liquid/events/MemoryEventStore.hpp"
#include "liquid/events/Replay.hpp"

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
namespace liquid::detail {

namespace {


std::pair<std::string, Value> serialized_topology(
    const TopologyMutation& topology,
    WorldInstanceId runtimeWorld,
    SessionId session
) {
    std::string key = topology.key;
    const std::string world = std::to_string(runtimeWorld);
    const std::string logicalWorld = std::to_string(session.value);
    for (const std::string prefix : {"behavior:", "access:"}) {
        const std::string actualPrefix = prefix + world + ":";
        if (key.starts_with(actualPrefix)) {
            key.replace(prefix.size(), world.size(), logicalWorld);
            break;
        }
    }

    Value value = topology.value;
    if (value.kind() == Value::Kind::Object) {
        Value::Object normalized = value.as_object();
        for (const char* field : {"world", "slot_world", "behavior_world"}) {
            const auto found = normalized.find(field);
            if (found != normalized.end())
                found->second = Value{session.value};
        }
        value = Value{std::move(normalized)};
    }
    return {std::move(key), std::move(value)};
}

}

}

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

void Runtime::record_world_evidence() {
    for (const auto& topology : ownedWorld.topology_mutations()) {
        auto [key, value] = liquid::detail::serialized_topology(
            topology, ownedWorld.instance_id(), effectsState->session);
        effectsState->append(liquid::EventType::TopologyChanged,
            liquid::detail::event_payload({
                {"key", liquid::Value{std::move(key)}},
                {"value", std::move(value)},
                {"removed", liquid::Value{topology.removed}}
            }));
    }
    ownedWorld.clear_topology_mutations();
    for (const auto& mutation : ownedWorld.component_mutations()) {
        effectsState->append(
            mutation.removed
                ? liquid::EventType::ComponentRemoved
                : liquid::EventType::ComponentMutated,
            liquid::detail::event_payload({
                {"key", liquid::Value{
                    std::to_string(mutation.target.type) + ":" + mutation.name}},
                {"value", mutation.after},
                {"type", liquid::Value{
                    static_cast<std::uint64_t>(mutation.target.type)}},
                {"slot_world", liquid::Value{effectsState->session.value}},
                {"slot", liquid::Value{
                    static_cast<std::uint64_t>(mutation.target.slot.slot)}},
                {"generation", liquid::Value{
                    static_cast<std::uint64_t>(mutation.target.slot.generation)}},
                {"name", liquid::Value{mutation.name}},
                {"before", mutation.before},
                {"after", mutation.after}
            }));
    }
    ownedWorld.clear_component_mutations();

    for (const auto& lifecycle : ownedWorld.intent_lifecycle_records()) {
        const auto& intent = lifecycle.intent;
        const std::string key = "intent:" +
            std::to_string(effectsState->session.value) + ":" +
            std::to_string(intent.id.slot) + ":" +
            std::to_string(intent.id.generation);
        if (!lifecycle.created) {
            effectsState->append(liquid::EventType::IntentDestroyed,
                liquid::detail::event_payload({
                    {"key", liquid::Value{key}},
                    {"intent_world", liquid::Value{effectsState->session.value}},
                    {"intent_slot", liquid::Value{
                        static_cast<std::uint64_t>(intent.id.slot)}},
                    {"intent_generation", liquid::Value{
                        static_cast<std::uint64_t>(intent.id.generation)}}
                }));
            continue;
        }
        effectsState->append(liquid::EventType::IntentCreated,
            liquid::detail::event_payload({
                {"key", liquid::Value{key}},
                {"value", intent.encodedValue},
                {"intent_world", liquid::Value{effectsState->session.value}},
                {"intent_slot", liquid::Value{
                    static_cast<std::uint64_t>(intent.id.slot)}},
                {"intent_generation", liquid::Value{
                    static_cast<std::uint64_t>(intent.id.generation)}},
                {"owner_world", liquid::Value{effectsState->session.value}},
                {"owner_slot", liquid::Value{
                    static_cast<std::uint64_t>(intent.owner.slot)}},
                {"owner_generation", liquid::Value{
                    static_cast<std::uint64_t>(intent.owner.generation)}},
                {"type", liquid::Value{
                    static_cast<std::uint64_t>(intent.target.type)}},
                {"target_world", liquid::Value{effectsState->session.value}},
                {"target_slot", liquid::Value{
                    static_cast<std::uint64_t>(intent.target.slot.slot)}},
                {"target_generation", liquid::Value{
                    static_cast<std::uint64_t>(intent.target.slot.generation)}},
                {"priority", liquid::Value{
                    static_cast<std::uint64_t>(intent.priority)}},
                {"name", liquid::Value{intent.name}},
                {"sequence", liquid::Value{intent.sequence}},
                {"lifetime", liquid::Value{
                    intent.lifetime.kind == IntentLifetimeKind::Persistent
                        ? "persistent" : "until-time"}},
                {"expires_at", liquid::Value{intent.lifetime.expiresAt}}
            }));
    }
    ownedWorld.clear_intent_lifecycle_records();

    // GCOVR_EXCL_START: script evidence is produced only through the Lua
    // boundary, which the Core-only coverage gate excludes by design; the
    // Lua-enabled suite (test_lua_lifecycle, test_lua_behavior) covers it.
    for (const auto& script : ownedWorld.script_execution_evidence()) {
        liquid::Value::Object payload;
        payload.emplace("key", liquid::Value{
            "script:" + std::to_string(effectsState->session.value) + ":" +
            std::to_string(script.owner.slot) + ":" +
            std::to_string(script.owner.generation) + ":" +
            std::to_string(script.now)});
        payload.emplace("owner_world", liquid::Value{effectsState->session.value});
        payload.emplace("owner_slot", liquid::Value{
            static_cast<std::uint64_t>(script.owner.slot)});
        payload.emplace("owner_generation", liquid::Value{
            static_cast<std::uint64_t>(script.owner.generation)});
        payload.emplace("now", liquid::Value{script.now});
        payload.emplace("source_included", liquid::Value{script.sourceIncluded});
        payload.emplace("source_hash", liquid::Value{script.sourceHash});
        if (script.sourceIncluded)
            payload.emplace("source", liquid::Value{script.source});
        payload.emplace("status", liquid::Value{
            static_cast<std::uint64_t>(script.status)});
        payload.emplace("diagnostic", liquid::Value{script.diagnostic});
        payload.emplace("created_intents", liquid::Value{
            static_cast<std::uint64_t>(script.createdIntentCount)});
        effectsState->append(
            liquid::EventType::ScriptExecuted,
            liquid::Value{std::move(payload)});
    }
    // GCOVR_EXCL_STOP
    ownedWorld.clear_script_execution_evidence();
}

std::vector<ResolvedEffect> Runtime::apply_selections(
    const FrameLog& frame
) {
    std::vector<ResolvedEffect> effects;
    std::set<ComponentTarget> selectedExternalComponents;
    for (const auto& [type, selections] : frame.intent_selections) {
        for (const auto& [name, id] : selections) {
            const Intent& selected = ownedWorld.intent(id);
            const ComponentTarget target = selected.target;
            const auto configured = componentControls.find(target);
            const ComponentControl control = configured == componentControls.end()
                ? ComponentControl::SelectionOnly
                : configured->second;

            if (control == ComponentControl::SelectionOnly)
                continue;
            if (control == ComponentControl::InternalState) {
                ownedWorld.replace_component_value(
                    target, name, selected.encodedValue);
                continue;
            }

            const auto binding = effectBindings.find(target);
            if (binding == effectBindings.end())
                throw std::logic_error(
                    "external component selection has no effect binding");
            auto effect = ownedWorld.encode_effect(
                target, name, selected.encodedValue);
            if (!effect)
                continue;
            if (effect->adapterRoute != binding->second.route ||
                effect->target != binding->second.target) {
                throw std::invalid_argument(
                    "effect codec output does not match the stable binding");
            }
            selectedExternalComponents.insert(target);
            effects.push_back(std::move(*effect));
        }
    }
    for (const auto& [target, binding] : effectBindings) {
        if (!selectedExternalComponents.contains(target))
            effectsState->clear_desire(binding.route, binding.target);
    }
    return effects;
}

void Runtime::project_authoritative_reports(
    const std::vector<EffectReport>& reports
) {
    for (const EffectReport& report : reports) {
        if (!effectsState->report_is_authoritative(report))
            continue;
        const auto bound = componentsByEffectTarget.find({
            report.adapterRoute.value(), report.target.value()});
        if (bound == componentsByEffectTarget.end())
            continue;
        const auto binding = effectBindings.find(bound->second);
        if (binding == effectBindings.end())
            throw std::logic_error("effect target binding is inconsistent");
        Value projected = ownedWorld.decode_observed(
            binding->second.component, *report.observedValue);
        ownedWorld.replace_component_value(
            binding->second.component,
            binding->second.name,
            projected);
    }
}

void Runtime::project_authoritative_observations(
    const std::vector<ExternalObservation>& observations
) {
    for (const ExternalObservation& observation : observations) {
        if (!effectsState->observation_is_authoritative(observation))
            continue;
        const auto bound = componentsByEffectTarget.find({
            observation.adapterRoute.value(), observation.target.value()});
        if (bound == componentsByEffectTarget.end())
            continue;
        const auto binding = effectBindings.find(bound->second);
        if (binding == effectBindings.end())
            throw std::logic_error("effect target binding is inconsistent");
        Value projected = ownedWorld.decode_observed(
            binding->second.component, observation.observedValue);
        ownedWorld.replace_component_value(
            binding->second.component,
            binding->second.name,
            projected);
    }
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
                        {"value", liquid::Value{effectsState->session.value}},
                        {"type", liquid::Value{static_cast<std::uint64_t>(type)}},
                        {"name", liquid::Value{name}},
                        {"intent_world", liquid::Value{effectsState->session.value}},
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
        effectsState->store->flush();
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
            effectsState->store->flush();
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
    return effectsState->feedback.sender;
}

std::optional<liquid::Value> Runtime::observed_state(
    const liquid::AdapterRoute& route,
    const liquid::EffectTarget& target
) const {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    const auto found = effectsState->observed.find(
        liquid::detail::target_key(route, target));
    if (found == effectsState->observed.end())
        return std::nullopt;
    return found->second;
}

std::optional<liquid::CommandStatus> Runtime::command_status(
    liquid::CommandId commandId
) const {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    const auto found = effectsState->commands.find(commandId.value);
    if (found == effectsState->commands.end())
        return std::nullopt;
    return found->second.status;
}

void Runtime::reconcile_indeterminate(
    const liquid::AdapterRoute& route,
    const liquid::EffectTarget& target,
    liquid::Value observedValue
) {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    observedValue.validate();
    const auto key = liquid::detail::target_key(route, target);
    const auto latest = effectsState->latestByTarget.find(key);
    if (latest == effectsState->latestByTarget.end() ||
        effectsState->commands.at(latest->second).status !=
            liquid::CommandStatus::Indeterminate) {
        throw std::logic_error("target has no indeterminate command to reconcile");
    }

    auto& state = effectsState->commands.at(latest->second);
    const liquid::CommandStatus reconciledStatus =
        state.command.effect.desiredValue == observedValue
        ? liquid::CommandStatus::Applied
        : liquid::CommandStatus::Failed;
    const liquid::Value reconciledObserved = observedValue;
    std::uint64_t revisionValue = state.command.commandId.value;
    const auto priorRevision = effectsState->observedRevisions.find(key);
    if (priorRevision != effectsState->observedRevisions.end()) {
        if (priorRevision->second.value == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("state revision exhausted");
        revisionValue = priorRevision->second.value + 1;
    }
    const liquid::StateRevision revision{revisionValue};
    liquid::EffectReport terminalReport{
        effectsState->session,
        state.command.commandId,
        route,
        target,
        reconciledStatus,
        reconciledStatus == liquid::CommandStatus::Applied
            ? std::optional<liquid::Value>{observedValue}
            : std::nullopt,
        "host reconciliation",
        state.command.issuedAtMs,
        revision
    };
    effectsState->transition(state, reconciledStatus, "host reconciliation");
    effectsState->append(liquid::EventType::ObservedStateChanged,
        liquid::detail::event_payload({
            {"key", liquid::Value{route.value() + ":" + target.value()}},
            {"value", observedValue},
            {"command_id", liquid::Value{state.command.commandId.value}},
            {"route", liquid::Value{route.value()}},
            {"target", liquid::Value{target.value()}},
            {"state_revision", liquid::Value{revision.value}},
            {"observed", std::move(observedValue)}
        }));
    state.terminalReport = std::move(terminalReport);
    effectsState->observed.insert_or_assign(key, reconciledObserved);
    effectsState->observedRevisions.insert_or_assign(key, revision);
    effectsState->authoritativeByTarget.insert_or_assign(
        key, state.command.commandId.value);
    effectsState->store->flush();
}

RecordId Runtime::checkpoint() {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    if (frameInProgress)
        throw std::logic_error("runtime checkpoint is unavailable during a frame");
    record_world_evidence();
    ReplayProjector projector;
    const auto records = effectsState->store->read_all();
    const SerializedWorldState state = projector.project(
        effectsState->store->metadata(), records);
    const RecordId checkpointId = effectsState->store->checkpoint(
        projector.checkpoint_payload(state), Durability::Durable);
    effectsState->store->flush();
    return checkpointId;
}

const FrameLog& Runtime::last_frame_log() const {
    ensure_owner_thread();
    return latestFrameLog;
}

}
