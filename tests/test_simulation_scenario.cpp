#include "SimulationScenario.hpp"

#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

using namespace liquid::simulation;

namespace {

class RecordingObserver : public SimulationObserver {
public:
    std::vector<std::string> events;
    std::vector<int> selectedBrightness;
    std::vector<int> actualBrightness;
    std::vector<IntentId> disappeared;

    void run_started(const SimulationOptions&) override { events.push_back("run_started"); }
    void world_ready(BehaviorId, ComponentTypeId, ComponentSlotId, int actual) override {
        events.push_back("world_ready");
        actualBrightness.push_back(actual);
    }
    void frame_started(FrameNumber, IntentTime) override { events.push_back("frame_started"); }
    void script_started(FrameNumber, IntentTime) override { events.push_back("script_started"); }
    void script_finished(
        FrameNumber,
        IntentTime,
        const liquid::scripting::LuaExecutionResult&
    ) override {
        events.push_back("script_finished");
    }
    void intent_created(FrameNumber, IntentTime, const IntentSnapshot&) override {
        events.push_back("intent_created");
    }
    void frame_completed(const FrameLog&) override { events.push_back("frame_completed"); }
    void intent_selected(FrameNumber, IntentTime, const IntentSnapshot& intent) override {
        events.push_back("intent_selected");
        selectedBrightness.push_back(intent.brightness);
    }
    void component_snapshot(FrameNumber, IntentTime, int actual) override {
        events.push_back("component_snapshot");
        actualBrightness.push_back(actual);
    }
    void intent_disappeared(FrameNumber, IntentTime, IntentId id) override {
        events.push_back("intent_disappeared");
        disappeared.push_back(id);
    }
    void run_completed(const SimulationOutcome&) override { events.push_back("run_completed"); }
    void run_failed(const SimulationOutcome&) override { events.push_back("run_failed"); }
};

class FaultingObserver : public SimulationObserver {
public:
    SimulationOutcome failure;

    void script_started(FrameNumber, IntentTime) override {
        throw std::runtime_error("observer boundary failure");
    }

    void run_failed(const SimulationOutcome& outcome) override {
        failure = outcome;
    }
};

}

int main() {
    const std::string source = R"(
        local light = access.Light.officeLight
        light.propose({ value = { brightness = 30 }, priority = "low" })
        light.propose({ value = { brightness = 70 }, priority = "high", duration_ms = 5 })
    )";

    RecordingObserver observer;
    SimulationOutcome outcome = run_scenario({10, source, {100, 105}}, observer);

    assert(outcome.status == SimulationStatus::Success);
    assert(outcome.script.succeeded());
    assert(outcome.script.createdIntents == std::vector<IntentId>({1, 2}));
    assert(outcome.finalBrightness == 10);
    assert(outcome.trackingSystemRuns == 2);
    assert(outcome.framesCompleted == 2);
    assert(!outcome.faulted);
    assert(observer.selectedBrightness == std::vector<int>({70, 30}));
    assert(observer.actualBrightness == std::vector<int>({10, 10, 10}));
    assert(observer.disappeared == std::vector<IntentId>({2}));
    assert(observer.events.front() == "run_started");
    assert(observer.events.back() == "run_completed");

    RecordingObserver failureObserver;
    SimulationOutcome failure = run_scenario({10, "error('bounded failure')", {100}}, failureObserver);
    assert(failure.status == SimulationStatus::ScriptError);
    assert(failure.script.status == liquid::scripting::LuaExecutionStatus::RuntimeError);
    assert(failure.framesCompleted == 1);
    assert(!failure.faulted);
    assert(failureObserver.events.back() == "run_completed");

    FaultingObserver faultingObserver;
    SimulationOutcome faulted = run_scenario({10, source, {100}}, faultingObserver);
    assert(faulted.status == SimulationStatus::HostError);
    assert(faulted.faulted);
    assert(faulted.framesCompleted == 0);
    assert(faulted.failurePhase == "run_systems");
    assert(faulted.diagnostic.find("observer boundary failure") != std::string::npos);
    assert(faultingObserver.failure.faulted);
}
