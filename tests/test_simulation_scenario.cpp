#include "SimulationScenario.hpp"

#include <cassert>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace liquid::simulation;

namespace {

const std::string lifecycleSource = R"(
function on_start(frame)
    local light = access.Light.officeLight
    solid.watch(light)
    light.propose({
        name = "fallback",
        value = { brightness = 30 },
        priority = "low",
        lifetime = "persistent"
    })
    light.propose({
        name = "boost",
        value = { brightness = 70 },
        priority = "high",
        duration_ms = 5
    })
end
)";

class RecordingObserver : public SimulationObserver {
public:
    std::vector<std::string> events;
    std::vector<int> selectedBrightness;
    std::vector<int> actualBrightness;
    std::vector<std::optional<int>> deviceBrightness;
    std::vector<liquid::IntentId> created;
    std::vector<liquid::IntentId> disappeared;
    std::size_t runtimeRecords = 0;

    void run_started(const SimulationOptions&) override {
        events.push_back("run_started");
    }
    void world_ready(
        liquid::BehaviorId,
        liquid::ComponentTypeId,
        liquid::ComponentSlotId,
        int actual
    ) override {
        events.push_back("world_ready");
        actualBrightness.push_back(actual);
    }
    void frame_started(liquid::FrameNumber, liquid::IntentTime) override {
        events.push_back("frame_started");
    }
    void script_started(liquid::FrameNumber, liquid::IntentTime) override {
        events.push_back("script_started");
    }
    void script_finished(
        liquid::FrameNumber,
        liquid::IntentTime,
        const liquid::scripting::LuaExecutionResult&
    ) override {
        events.push_back("script_finished");
    }
    void intent_created(
        liquid::FrameNumber,
        liquid::IntentTime,
        const IntentSnapshot& intent
    ) override {
        events.push_back("intent_created");
        created.push_back(intent.id);
    }
    void intent_disappeared(
        liquid::FrameNumber,
        liquid::IntentTime,
        liquid::IntentId id
    ) override {
        events.push_back("intent_disappeared");
        disappeared.push_back(id);
    }
    void intent_selected(
        liquid::FrameNumber,
        liquid::IntentTime,
        const IntentSnapshot& intent
    ) override {
        events.push_back("intent_selected");
        selectedBrightness.push_back(intent.brightness);
    }
    void runtime_record(const liquid::EventRecord&) override {
        runtimeRecords++;
    }
    void frame_completed(const liquid::FrameResult&) override {
        events.push_back("frame_completed");
    }
    void component_snapshot(
        liquid::FrameNumber,
        liquid::IntentTime,
        int actual,
        std::optional<int> device
    ) override {
        events.push_back("component_snapshot");
        actualBrightness.push_back(actual);
        deviceBrightness.push_back(device);
    }
    void run_completed(const SimulationOutcome&) override {
        events.push_back("run_completed");
    }
    void run_failed(const SimulationOutcome&) override {
        events.push_back("run_failed");
    }
};

class FaultingObserver : public SimulationObserver {
public:
    SimulationOutcome failure;

    void script_started(liquid::FrameNumber, liquid::IntentTime) override {
        throw std::runtime_error("observer boundary failure");
    }

    void run_failed(const SimulationOutcome& outcome) override {
        failure = outcome;
    }
};

SimulationOptions options_with(
    const std::string& source,
    std::vector<liquid::IntentTime> frameTimes
) {
    SimulationOptions options;
    options.initialBrightness = 10;
    options.source = source;
    options.frameTimes = std::move(frameTimes);
    return options;
}

void test_full_loop_success() {
    RecordingObserver observer;
    const SimulationOutcome outcome = run_scenario(
        options_with(lifecycleSource, {100, 105, 110}), observer);

    assert(outcome.status == SimulationStatus::Success);
    assert(outcome.script.succeeded());
    assert(!outcome.faulted);
    assert(outcome.framesCompleted == 3);
    assert(outcome.trackingSystemRuns == 3);

    // The loop is truthful: frame 100 selects and commands 70 while the
    // confirmed component stays 10; frame 105 applies the device report
    // before behavior logic and expires the boost, so 70 is confirmed and
    // 30 is commanded; frame 110 confirms 30.
    assert(outcome.finalBrightness == 30);
    assert(outcome.deviceBrightness == std::optional<int>(30));
    assert(outcome.commandsIssued == 2);
    assert(outcome.reportsApplied == 2);
    assert(outcome.observationsApplied == 0);

    assert(observer.selectedBrightness == std::vector<int>({70, 30, 30}));
    assert(observer.actualBrightness == std::vector<int>({10, 10, 70, 30}));
    assert(observer.deviceBrightness ==
        std::vector<std::optional<int>>({70, 30, 30}));
    assert(observer.created.size() == 2);
    assert(static_cast<std::uint32_t>(observer.created[0]) == 1);
    assert(static_cast<std::uint32_t>(observer.created[1]) == 2);
    assert(observer.disappeared ==
        std::vector<liquid::IntentId>({observer.created[1]}));
    assert(observer.runtimeRecords > 0);
    assert(observer.events.front() == "run_started");
    assert(observer.events.back() == "run_completed");
}

void test_top_level_capability_calls_are_rejected() {
    const std::string topLevelSource = R"(
        local light = access.Light.officeLight
        light.propose({ value = { brightness = 70 }, priority = "high" })
    )";
    RecordingObserver observer;
    const SimulationOutcome outcome = run_scenario(
        options_with(topLevelSource, {100}), observer);

    assert(outcome.status == SimulationStatus::ScriptError);
    assert(outcome.script.status ==
        liquid::scripting::LuaExecutionStatus::InvalidProposal);
    assert(outcome.framesCompleted == 1);
    assert(!outcome.faulted);
    assert(outcome.finalBrightness == 10);
    assert(!outcome.deviceBrightness.has_value());
    assert(outcome.commandsIssued == 0);
    assert(observer.created.empty());
    assert(observer.selectedBrightness.empty());
    assert(observer.events.back() == "run_completed");
}

void test_script_error_is_bounded() {
    const std::string failingSource =
        "function on_start(frame)\n"
        "    error('bounded failure')\n"
        "end\n";
    RecordingObserver observer;
    const SimulationOutcome outcome = run_scenario(
        options_with(failingSource, {100}), observer);

    assert(outcome.status == SimulationStatus::ScriptError);
    assert(outcome.script.status ==
        liquid::scripting::LuaExecutionStatus::RuntimeError);
    assert(outcome.framesCompleted == 1);
    assert(!outcome.faulted);
    assert(outcome.finalBrightness == 10);
    assert(outcome.commandsIssued == 0);
    assert(observer.created.empty());
    assert(observer.events.back() == "run_completed");
}

void test_rejected_command_creates_no_false_confirmed_state() {
    SimulationOptions options = options_with(lifecycleSource, {100, 105, 110});
    options.adapterOutcome = liquid::CommandStatus::Rejected;
    RecordingObserver observer;
    const SimulationOutcome outcome = run_scenario(options, observer);

    assert(outcome.status == SimulationStatus::Success);
    assert(!outcome.faulted);
    assert(outcome.finalBrightness == 10);
    assert(!outcome.deviceBrightness.has_value());
    // Rejected reports are received into the feedback phase but never
    // projected into the component, and the unconfirmed desire is
    // re-commanded on the next frame.
    assert(outcome.reportsApplied == 2);
    assert(outcome.commandsIssued == 3);
    assert(observer.actualBrightness ==
        std::vector<int>({10, 10, 10, 10}));
    assert(observer.deviceBrightness ==
        std::vector<std::optional<int>>(
            {std::nullopt, std::nullopt, std::nullopt}));
}

void test_silent_adapter_creates_no_false_confirmed_state() {
    SimulationOptions options = options_with(lifecycleSource, {100, 105, 110});
    options.silent = true;
    RecordingObserver observer;
    const SimulationOutcome outcome = run_scenario(options, observer);

    assert(outcome.status == SimulationStatus::Success);
    assert(!outcome.faulted);
    assert(outcome.finalBrightness == 10);
    assert(outcome.reportsApplied == 0);
    assert(observer.actualBrightness ==
        std::vector<int>({10, 10, 10, 10}));
}

void test_projection_precedes_behavior_logic() {
    // If the device report were applied after script logic, the change
    // callback could not observe the confirmed 70 in the same frame the
    // report lands, and 55 would never be proposed or selected.
    const std::string reactiveSource = R"(
function on_start(frame)
    local light = access.Light.officeLight
    solid.watch(light)
    light.propose({
        name = "boost",
        value = { brightness = 70 },
        priority = "high",
        duration_ms = 5
    })
end

function on_components_changed(frame, changes)
    local light = access.Light.officeLight
    if light.value.brightness == 70 then
        light.propose({
            name = "confirm_react",
            value = { brightness = 55 },
            priority = "high",
            duration_ms = 5
        })
    end
end
)";
    RecordingObserver observer;
    const SimulationOutcome outcome = run_scenario(
        options_with(reactiveSource, {100, 105}), observer);

    assert(outcome.status == SimulationStatus::Success);
    assert(!outcome.faulted);
    assert(observer.selectedBrightness == std::vector<int>({70, 55}));
    assert(observer.actualBrightness == std::vector<int>({10, 10, 70}));
}

void test_observer_failure_is_a_host_error() {
    FaultingObserver observer;
    const SimulationOutcome outcome = run_scenario(
        options_with(lifecycleSource, {100}), observer);

    assert(outcome.status == SimulationStatus::HostError);
    // The observer fails at the script_started boundary, which is emitted
    // after the frame ran; the completed frame stays counted and the
    // Runtime itself stays healthy.
    assert(outcome.framesCompleted == 1);
    assert(!outcome.faulted);
    assert(outcome.diagnostic.find("observer boundary failure")
        != std::string::npos);
    assert(observer.failure.status == SimulationStatus::HostError);
}

void test_invalid_options_are_a_host_error() {
    RecordingObserver observer;
    const SimulationOutcome outcome = run_scenario(
        options_with(lifecycleSource, {}), observer);

    assert(outcome.status == SimulationStatus::HostError);
    assert(outcome.framesCompleted == 0);
    assert(!outcome.diagnostic.empty());
    assert(observer.events.back() == "run_failed");
}

}

int main() {
    test_full_loop_success();
    test_top_level_capability_calls_are_rejected();
    test_script_error_is_bounded();
    test_rejected_command_creates_no_false_confirmed_state();
    test_silent_adapter_creates_no_false_confirmed_state();
    test_projection_precedes_behavior_logic();
    test_observer_failure_is_a_host_error();
    test_invalid_options_are_a_host_error();
}
