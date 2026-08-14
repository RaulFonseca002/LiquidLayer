#pragma once

#include "liquid/Runtime.hpp"
#include "liquid/events/EventStore.hpp"
#include "liquid/scripting/LuaBehaviorRunner.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace liquid::simulation {

struct SimulationOptions {
    int initialBrightness = 0;
    std::string source;
    std::vector<IntentTime> frameTimes;
    FeedbackTiming feedbackTiming = FeedbackTiming::Deferred;
    std::uint64_t latencyMs = 0;
    CommandStatus adapterOutcome = CommandStatus::Applied;
    std::size_t duplicateReports = 0;
    bool silent = false;
    bool reverseDelivery = false;
};

struct IntentSnapshot {
    IntentId id{};
    BehaviorId owner{};
    ComponentTypeId type = InvalidComponentTypeId;
    std::string typeName;
    std::string component;
    std::string name;
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
    std::optional<int> deviceBrightness;
    std::size_t commandsIssued = 0;
    std::size_t reportsApplied = 0;
    std::size_t observationsApplied = 0;
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
    virtual void intent_created(
        FrameNumber frame, IntentTime now, const IntentSnapshot& intent);
    virtual void intent_disappeared(
        FrameNumber frame, IntentTime now, IntentId id);
    virtual void intent_selected(
        FrameNumber frame, IntentTime now, const IntentSnapshot& intent);
    virtual void runtime_record(const EventRecord& record);
    virtual void frame_completed(const FrameResult& frame);
    virtual void component_snapshot(
        FrameNumber frame,
        IntentTime now,
        int actualBrightness,
        std::optional<int> deviceBrightness
    );
    virtual void run_completed(const SimulationOutcome& outcome);
    virtual void run_failed(const SimulationOutcome& outcome);
};

SimulationOutcome run_scenario(
    const SimulationOptions& options,
    SimulationObserver& observer);

std::string lua_status_name(scripting::LuaExecutionStatus status);
std::string priority_name(IntentPriority priority);
std::string lifetime_name(IntentLifetimeKind kind);

}
