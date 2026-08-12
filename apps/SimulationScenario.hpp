#pragma once

#include "liquid/Runtime.hpp"
#include "liquid/scripting/LuaBehaviorRunner.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace liquid::simulation {

struct SimulationOptions {
    int initialBrightness = 0;
    std::string source;
    std::vector<IntentTime> frameTimes;
};

struct IntentSnapshot {
    IntentId id = 0;
    BehaviorId owner = 0;
    ComponentTypeId type = InvalidComponentTypeId;
    std::string typeName;
    std::string component;
    int brightness = 0;
    IntentPriority priority = IntentPriority::Low;
    IntentLifetime lifetime = IntentLifetime::persistent();
};

enum class SimulationStatus {
    Success,
    ScriptError,
    HostError
};

struct SimulationOutcome {
    SimulationStatus status = SimulationStatus::Success;
    scripting::LuaExecutionResult script;
    int finalBrightness = 0;
    std::size_t trackingSystemRuns = 0;
    FrameNumber framesCompleted = 0;
    bool faulted = false;
    std::string failurePhase;
    std::string diagnostic;
};

class SimulationObserver {
public:
    virtual ~SimulationObserver() = default;

    virtual void run_started(const SimulationOptions& options);
    virtual void world_ready(
        BehaviorId behavior,
        ComponentTypeId lightType,
        ComponentSlotId lightSlot,
        int actualBrightness
    );
    virtual void frame_started(FrameNumber frame, IntentTime now);
    virtual void script_started(FrameNumber frame, IntentTime now);
    virtual void script_finished(
        FrameNumber frame,
        IntentTime now,
        const scripting::LuaExecutionResult& result
    );
    virtual void intent_created(FrameNumber frame, IntentTime now, const IntentSnapshot& intent);
    virtual void frame_completed(const FrameLog& frame);
    virtual void intent_selected(FrameNumber frame, IntentTime now, const IntentSnapshot& intent);
    virtual void component_snapshot(FrameNumber frame, IntentTime now, int actualBrightness);
    virtual void intent_disappeared(FrameNumber frame, IntentTime now, IntentId id);
    virtual void run_completed(const SimulationOutcome& outcome);
    virtual void run_failed(const SimulationOutcome& outcome);
};

SimulationOutcome run_scenario(const SimulationOptions& options, SimulationObserver& observer);

std::string lua_status_name(scripting::LuaExecutionStatus status);
std::string priority_name(IntentPriority priority);
std::string lifetime_name(IntentLifetimeKind kind);

}
