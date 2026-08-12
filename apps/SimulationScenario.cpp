#include "SimulationScenario.hpp"

#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>

namespace liquid::simulation {

namespace {

using scripting::LuaBehaviorRunner;
using scripting::LuaComponentCodec;
using scripting::LuaExecutionResult;
using scripting::LuaExecutionStatus;
using scripting::LuaValue;

constexpr char LightTypeName[] = "Light";
constexpr char LightComponentName[] = "officeLight";

struct Light {
    int brightness = 0;
};

LuaComponentCodec<Light> light_codec() {
    return {
        [](const Light& light) {
            return LuaValue::Table{{"brightness", LuaValue{light.brightness}}};
        },
        [](const LuaValue& value) {
            const auto& table = value.as_table();
            if (table.size() != 1 || !table.contains("brightness"))
                throw std::runtime_error("Light requires exactly brightness");

            std::int64_t brightness = table.at("brightness").as_integer();
            if (brightness < 0 || brightness > 100)
                throw std::runtime_error("brightness must be between 0 and 100");

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
    ComponentTarget target = world.intent_target(id);
    if (target.type != lightType.id)
        throw std::runtime_error("unexpected intent component type");

    return {
        id,
        world.intent_owner(id),
        target.type,
        LightTypeName,
        LightComponentName,
        intent.value.brightness,
        intent.priority,
        intent.lifetime
    };
}

class LuaScenarioSystem : public System {
private:
    LuaBehaviorRunner* runner;
    SimulationObserver* observer;
    ComponentType<Light> lightType;
    BehaviorId owner;
    std::string source;
    bool hasRun = false;
    LuaExecutionResult executionResult;

public:
    LuaScenarioSystem(
        LuaBehaviorRunner& behaviorRunner,
        SimulationObserver& simulationObserver,
        ComponentType<Light> componentType,
        BehaviorId behavior,
        std::string scriptSource
    )
        : runner(&behaviorRunner),
          observer(&simulationObserver),
          lightType(componentType),
          owner(behavior),
          source(std::move(scriptSource)) {
    }

    void run(World& world, FrameNumber frame, IntentTime now) override {
        if (hasRun)
            return;

        observer->script_started(frame, now);
        executionResult = runner->execute(world, owner, now, source);
        hasRun = true;
        observer->script_finished(frame, now, executionResult);

        for (IntentId id : executionResult.createdIntents) {
            if (world.intent_exists(id))
                observer->intent_created(frame, now, snapshot_intent(world, lightType, id));
        }
    }

    const LuaExecutionResult& result() const {
        return executionResult;
    }
};

class TrackingSystem : public System {
private:
    std::size_t completedRuns = 0;

public:
    void run(World&, FrameNumber, IntentTime) override {
        completedRuns++;
    }

    std::size_t runs() const {
        return completedRuns;
    }
};

void validate_options(const SimulationOptions& options) {
    if (options.initialBrightness < 0 || options.initialBrightness > 100)
        throw std::invalid_argument("initial brightness must be between 0 and 100");
    if (options.frameTimes.empty())
        throw std::invalid_argument("at least one frame time is required");

    IntentTime previous = 0;
    bool first = true;
    for (IntentTime now : options.frameTimes) {
        if (now > static_cast<IntentTime>(std::numeric_limits<std::int64_t>::max()))
            throw std::invalid_argument("frame time exceeds the Lua integer range");
        if (!first && now < previous)
            throw std::invalid_argument("frame times must be nondecreasing");
        previous = now;
        first = false;
    }
}

}

void SimulationObserver::run_started(const SimulationOptions&) {}
void SimulationObserver::world_ready(BehaviorId, ComponentTypeId, ComponentSlotId, int) {}
void SimulationObserver::frame_started(FrameNumber, IntentTime) {}
void SimulationObserver::script_started(FrameNumber, IntentTime) {}
void SimulationObserver::script_finished(
    FrameNumber,
    IntentTime,
    const scripting::LuaExecutionResult&
) {}
void SimulationObserver::intent_created(FrameNumber, IntentTime, const IntentSnapshot&) {}
void SimulationObserver::frame_completed(const FrameLog&) {}
void SimulationObserver::intent_selected(FrameNumber, IntentTime, const IntentSnapshot&) {}
void SimulationObserver::component_snapshot(FrameNumber, IntentTime, int) {}
void SimulationObserver::intent_disappeared(FrameNumber, IntentTime, IntentId) {}
void SimulationObserver::run_completed(const SimulationOutcome&) {}
void SimulationObserver::run_failed(const SimulationOutcome&) {}

SimulationOutcome run_scenario(const SimulationOptions& options, SimulationObserver& observer) {
    SimulationOutcome outcome;
    outcome.finalBrightness = options.initialBrightness;
    std::unique_ptr<LuaBehaviorRunner> runner;
    std::unique_ptr<Runtime> runtime;

    try {
        observer.run_started(options);
        validate_options(options);
        runner = std::make_unique<LuaBehaviorRunner>();
        runtime = std::make_unique<Runtime>();
        World& world = runtime->world();
        ComponentType<Light> lightType = world.register_component<Light>(LightTypeName);
        world.add_component(lightType, LightComponentName, Light{options.initialBrightness});

        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(
            lightType,
            behavior,
            LightComponentName,
            ComponentAccessMode::ReadWrite
        );
        ComponentSlotId slot = world.get_components(lightType, behavior).at(LightComponentName);

        runner->expose_component(lightType, LightTypeName, light_codec());
        world.register_system<LuaScenarioSystem>(
            Signature{},
            *runner,
            observer,
            lightType,
            behavior,
            options.source
        );
        world.register_system<TrackingSystem>(Signature{});
        observer.world_ready(behavior, lightType.id, slot, options.initialBrightness);

        std::map<ComponentTypeId, std::map<ComponentName, ComponentSlotId>> resolutions{
            {lightType.id, {{LightComponentName, slot}}}
        };
        std::vector<IntentId> observedIntents;
        bool scriptIntentsCaptured = false;

        for (IntentTime now : options.frameTimes) {
            FrameNumber frameNumber = runtime->frame();
            observer.frame_started(frameNumber, now);
            FrameLog frame = runtime->run_frame(now, resolutions);
            const LuaExecutionResult& script = world.get_system<LuaScenarioSystem>().result();

            if (!scriptIntentsCaptured) {
                observedIntents = script.createdIntents;
                scriptIntentsCaptured = true;
            }

            observer.frame_completed(frame);
            auto intent = observedIntents.begin();
            while (intent != observedIntents.end()) {
                if (!world.intent_exists(*intent)) {
                    IntentId disappeared = *intent;
                    observer.intent_disappeared(frame.frame, frame.now, disappeared);
                    intent = observedIntents.erase(intent);
                } else {
                    ++intent;
                }
            }

            for (const auto& [type, selections] : frame.intent_selections) {
                if (type != lightType.id)
                    throw std::runtime_error("unexpected selected component type");
                for (const auto& [name, id] : selections) {
                    if (name != LightComponentName)
                        throw std::runtime_error("unexpected selected component name");
                    observer.intent_selected(frame.frame, frame.now, snapshot_intent(world, lightType, id));
                }
            }

            const Light* light = world.get_component_named(lightType, LightComponentName);
            if (!light)
                throw std::runtime_error("light component is unavailable");
            observer.component_snapshot(frame.frame, frame.now, light->brightness);
        }

        const Light* finalLight = world.get_component_named(lightType, LightComponentName);
        if (!finalLight)
            throw std::runtime_error("final light state is unavailable");

        outcome.script = world.get_system<LuaScenarioSystem>().result();
        outcome.finalBrightness = finalLight->brightness;
        outcome.trackingSystemRuns = world.get_system<TrackingSystem>().runs();
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
