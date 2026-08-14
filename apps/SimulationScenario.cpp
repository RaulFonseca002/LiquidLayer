#include "SimulationScenario.hpp"

#include "liquid/ComponentCodec.hpp"
#include "liquid/events/MemoryEventStore.hpp"
#include "liquid/scripting/LuaLifecycleSystem.hpp"
#include "liquid/simulation/InMemoryAdapter.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>

namespace liquid::simulation {

namespace {

using scripting::LuaBehaviorRunner;
using scripting::LuaBehaviorScript;
using scripting::LuaComponentCodec;
using scripting::LuaExecutionResult;
using scripting::LuaExecutionStatus;
using scripting::LuaLifecycleSystem;
using scripting::LuaValue;

constexpr char LightTypeName[] = "Light";
constexpr char LightComponentName[] = "officeLight";
constexpr char ScriptComponentName[] = "lifecycle";
constexpr char AdapterRouteName[] = "scope.light";
constexpr char EffectTargetName[] = "office-device";

struct Light {
    int brightness = 0;
};

void validate_brightness(std::int64_t brightness) {
    if (brightness < 0 || brightness > 100)
        throw std::runtime_error("brightness must be between 0 and 100");
}

ComponentCodec<Light> component_codec() {
    return {
        [](const Light& light) {
            validate_brightness(light.brightness);
            return Value{static_cast<std::int64_t>(light.brightness)};
        },
        [](const Value& value) {
            const std::int64_t brightness = value.as_signed_integer();
            validate_brightness(brightness);
            return Light{static_cast<int>(brightness)};
        }
    };
}

EffectCodec<Light> effect_codec() {
    return {
        AdapterRoute{AdapterRouteName},
        [](const ComponentName&, const Light& light)
            -> std::optional<ResolvedEffect> {
            validate_brightness(light.brightness);
            return ResolvedEffect{
                AdapterRoute{AdapterRouteName},
                EffectTarget{EffectTargetName},
                Value{static_cast<std::int64_t>(light.brightness)}};
        },
        [](const Value& value) {
            const std::int64_t brightness = value.as_signed_integer();
            validate_brightness(brightness);
            return Light{static_cast<int>(brightness)};
        }
    };
}

LuaComponentCodec<Light> lua_codec() {
    return {
        [](const Light& light) {
            return LuaValue::Table{{"brightness", LuaValue{light.brightness}}};
        },
        [](const LuaValue& value) {
            const auto& table = value.as_table();
            if (table.size() != 1 || !table.contains("brightness"))
                throw std::runtime_error("Light requires exactly brightness");
            const std::int64_t brightness =
                table.at("brightness").as_integer();
            validate_brightness(brightness);
            return Light{static_cast<int>(brightness)};
        }
    };
}

IntentSnapshot snapshot_intent(
    World& world,
    ComponentType<Light> lightType,
    IntentId id
) {
    const ComponentIntent<Light>& intent = world.typed_intent(lightType, id);
    const ComponentTarget target = world.intent_target(id);
    if (target.type != lightType.id)
        throw std::runtime_error("unexpected intent component type");
    return {
        id,
        world.intent_owner(id),
        target.type,
        LightTypeName,
        LightComponentName,
        world.intent(id).name,
        intent.value.brightness,
        intent.priority,
        intent.lifetime};
}

class TrackingSystem final : public System {
private:
    std::size_t completedRuns = 0;

public:
    static constexpr std::string_view stableName =
        "liquid.scope.TrackingSystem";
    static constexpr std::uint32_t version = 1;

    void run(World&, FrameNumber, IntentTime) override {
        completedRuns++;
    }

    std::size_t runs() const {
        return completedRuns;
    }
};

void validate_options(const SimulationOptions& options) {
    validate_brightness(options.initialBrightness);
    if (options.frameTimes.empty())
        throw std::invalid_argument("at least one frame time is required");
    if (options.duplicateReports > 64)
        throw std::invalid_argument("duplicate reports must not exceed 64");
    if (options.adapterOutcome == CommandStatus::Pending ||
        options.adapterOutcome == CommandStatus::Superseded ||
        options.adapterOutcome == CommandStatus::Indeterminate ||
        options.adapterOutcome == CommandStatus::TimedOut) {
        throw std::invalid_argument("adapter outcome is not directly reportable");
    }
    IntentTime previous = 0;
    bool first = true;
    for (IntentTime now : options.frameTimes) {
        if (now > static_cast<IntentTime>(
                std::numeric_limits<std::int64_t>::max()))
            throw std::invalid_argument("frame time exceeds the Lua integer range");
        if (!first && now < previous)
            throw std::invalid_argument("frame times must be nondecreasing");
        previous = now;
        first = false;
    }
}

std::optional<int> brightness_of(const std::optional<Value>& value) {
    if (!value)
        return std::nullopt;
    const std::int64_t brightness = value->as_signed_integer();
    validate_brightness(brightness);
    return static_cast<int>(brightness);
}

}

void SimulationObserver::run_started(const SimulationOptions&) {}
void SimulationObserver::world_ready(
    BehaviorId, ComponentTypeId, ComponentSlotId, int) {}
void SimulationObserver::frame_started(FrameNumber, IntentTime) {}
void SimulationObserver::script_started(FrameNumber, IntentTime) {}
void SimulationObserver::script_finished(
    FrameNumber, IntentTime, const scripting::LuaExecutionResult&) {}
void SimulationObserver::intent_created(
    FrameNumber, IntentTime, const IntentSnapshot&) {}
void SimulationObserver::intent_disappeared(
    FrameNumber, IntentTime, IntentId) {}
void SimulationObserver::intent_selected(
    FrameNumber, IntentTime, const IntentSnapshot&) {}
void SimulationObserver::runtime_record(const EventRecord&) {}
void SimulationObserver::frame_completed(const FrameResult&) {}
void SimulationObserver::component_snapshot(
    FrameNumber, IntentTime, int, std::optional<int>) {}
void SimulationObserver::run_completed(const SimulationOutcome&) {}
void SimulationObserver::run_failed(const SimulationOutcome&) {}

SimulationOutcome run_scenario(
    const SimulationOptions& options,
    SimulationObserver& observer
) {
    SimulationOutcome outcome;
    outcome.finalBrightness = options.initialBrightness;
    std::unique_ptr<Runtime> runtime;

    try {
        observer.run_started(options);
        validate_options(options);

        EventStoreMetadata metadata;
        metadata.session = SessionId{1};
        metadata.engineVersion = "0.1.0-scope";
        metadata.feedbackTiming = options.feedbackTiming;
        MemoryEventStore store{metadata};
        RuntimeOptions runtimeOptions;
        runtimeOptions.sessionId = metadata.session;
        runtimeOptions.feedbackTiming = options.feedbackTiming;
        runtimeOptions.eventStore = &store;
        runtime = std::make_unique<Runtime>(runtimeOptions);
        World& world = runtime->world();

        const ComponentType<Light> lightType = world.register_component<Light>(
            "scope.Light", 1, component_codec());
        world.register_effect_codec(lightType, effect_codec());
        const auto scriptType = world.register_component<LuaBehaviorScript>(
            "liquid.LuaBehaviorScript",
            1,
            scripting::lua_behavior_script_codec());
        world.add_component(
            lightType,
            LightComponentName,
            Light{options.initialBrightness});
        world.add_component(
            scriptType,
            ScriptComponentName,
            LuaBehaviorScript{options.source, 1});

        const BehaviorId behavior = world.create_behavior();
        world.grant_component_access(
            lightType,
            behavior,
            LightComponentName,
            ComponentAccessMode::ReadWrite);
        world.grant_component_access(
            scriptType,
            behavior,
            ScriptComponentName,
            ComponentAccessMode::Read);
        const ComponentSlotId slot =
            world.get_components(lightType, behavior).at(LightComponentName);

        auto runner = std::make_shared<LuaBehaviorRunner>();
        runner->expose_component(lightType, LightTypeName, lua_codec());
        Signature scriptSignature;
        scriptSignature.set(scriptType.id);
        world.register_system<LuaLifecycleSystem>(
            scriptSignature,
            SystemPhase::Behavior,
            scriptType,
            runner,
            ScriptComponentName);
        world.register_system<TrackingSystem>(
            Signature{}, SystemPhase::Decision);

        AdapterBehavior adapterBehavior;
        adapterBehavior.latencyMs = options.latencyMs;
        adapterBehavior.outcome = options.adapterOutcome;
        adapterBehavior.duplicateReports = options.duplicateReports;
        adapterBehavior.silent = options.silent;
        adapterBehavior.reverseDelivery = options.reverseDelivery;
        auto adapter = std::make_shared<InMemoryAdapter>(
            AdapterRoute{AdapterRouteName}, adapterBehavior);
        runtime->register_adapter(adapter);
        runtime->bind_effect_component(
            lightType,
            LightComponentName,
            EffectTarget{EffectTargetName});
        observer.world_ready(
            behavior, lightType.id, slot, options.initialBrightness);

        std::set<IntentId> knownIntents;
        std::size_t emittedRecords = 0;
        for (IntentTime now : options.frameTimes) {
            const FrameNumber frameNumber = runtime->frame();
            adapter->deliver_through(now);
            observer.frame_started(frameNumber, now);
            observer.script_started(frameNumber, now);
            FrameInput input;
            input.now = now;
            FrameResult result = runtime->run_frame(std::move(input));

            const auto& lifecycle = world.get_system<LuaLifecycleSystem>();
            const LuaExecutionResult* script = lifecycle.last_result(behavior);
            if (!script)
                throw std::runtime_error("lifecycle script result is unavailable");
            outcome.script = *script;
            observer.script_finished(frameNumber, now, *script);

            std::set<IntentId> current;
            for (IntentId id : world.intents_owned_by(behavior)) {
                current.insert(id);
                if (!knownIntents.contains(id))
                    observer.intent_created(
                        frameNumber, now, snapshot_intent(world, lightType, id));
            }
            for (IntentId id : knownIntents) {
                if (!current.contains(id))
                    observer.intent_disappeared(frameNumber, now, id);
            }
            knownIntents = std::move(current);

            for (const auto& [type, selections] : result.frame.intent_selections) {
                if (type != lightType.id)
                    continue;
                for (const auto& [name, id] : selections) {
                    if (name == LightComponentName)
                        observer.intent_selected(
                            frameNumber, now, snapshot_intent(world, lightType, id));
                }
            }

            const std::vector<EventRecord> records = store.read_all();
            while (emittedRecords < records.size())
                observer.runtime_record(records[emittedRecords++]);
            observer.frame_completed(result);

            const Light* light = world.read_component(
                lightType, behavior, LightComponentName);
            if (!light)
                throw std::runtime_error("light component is unavailable");
            const std::optional<int> device = brightness_of(
                adapter->state(EffectTarget{EffectTargetName}));
            observer.component_snapshot(
                frameNumber, now, light->brightness, device);
            outcome.commandsIssued += result.commands.size();
            outcome.reportsApplied += result.reports.size();
            outcome.observationsApplied += result.observations.size();
        }

        const Light* finalLight = world.read_component(
            lightType, behavior, LightComponentName);
        if (!finalLight)
            throw std::runtime_error("final light state is unavailable");
        outcome.finalBrightness = finalLight->brightness;
        outcome.deviceBrightness = brightness_of(
            adapter->state(EffectTarget{EffectTargetName}));
        outcome.trackingSystemRuns =
            world.get_system<TrackingSystem>().runs();
        outcome.framesCompleted = runtime->frame();
        outcome.faulted = runtime->faulted();

        if (outcome.script.status == LuaExecutionStatus::HostError) {
            outcome.status = SimulationStatus::HostError;
            outcome.diagnostic = outcome.script.diagnostic;
            observer.run_failed(outcome);
        } else {
            outcome.status = outcome.script.succeeded()
                ? SimulationStatus::Success
                : SimulationStatus::ScriptError;
            if (!outcome.script.succeeded())
                outcome.diagnostic = outcome.script.diagnostic;
            observer.run_completed(outcome);
        }
        return outcome;
    } catch (const std::exception& exception) {
        outcome.status = SimulationStatus::HostError;
        outcome.diagnostic = exception.what();
    } catch (...) {
        outcome.status = SimulationStatus::HostError;
        outcome.diagnostic = "unknown exception";
    }

    if (runtime) {
        outcome.framesCompleted = runtime->frame();
        outcome.faulted = runtime->faulted();
        if (outcome.faulted) {
            const FrameLog& failedFrame = runtime->last_frame_log();
            outcome.failurePhase = failedFrame.failure_phase;
            if (outcome.diagnostic.empty())
                outcome.diagnostic = failedFrame.failure_message;
        }
    }
    observer.run_failed(outcome);
    return outcome;
}

std::string lua_status_name(scripting::LuaExecutionStatus status) {
    switch (status) {
    case LuaExecutionStatus::Success: return "success";
    case LuaExecutionStatus::InvalidBehavior: return "invalid_behavior";
    case LuaExecutionStatus::SourceLimitExceeded: return "source_limit_exceeded";
    case LuaExecutionStatus::SyntaxError: return "syntax_error";
    case LuaExecutionStatus::RuntimeError: return "runtime_error";
    case LuaExecutionStatus::InstructionLimitExceeded: return "instruction_limit_exceeded";
    case LuaExecutionStatus::MemoryLimitExceeded: return "memory_limit_exceeded";
    case LuaExecutionStatus::IntentLimitExceeded: return "intent_limit_exceeded";
    case LuaExecutionStatus::InvalidProposal: return "invalid_proposal";
    case LuaExecutionStatus::CommitFailed: return "commit_failed";
    case LuaExecutionStatus::HostError: return "host_error";
    }
    return "unknown";
}

std::string priority_name(IntentPriority priority) {
    switch (priority) {
    case IntentPriority::Low: return "low";
    case IntentPriority::Medium: return "medium";
    case IntentPriority::High: return "high";
    }
    return "unknown";
}

std::string lifetime_name(IntentLifetimeKind kind) {
    switch (kind) {
    case IntentLifetimeKind::Persistent: return "persistent";
    case IntentLifetimeKind::UntilTime: return "until_time";
    }
    return "unknown";
}

}
