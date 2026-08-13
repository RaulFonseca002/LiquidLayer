#include "liquid/events/MemoryEventStore.hpp"
#include "liquid/events/Replay.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

liquid::EventStoreMetadata metadata() {
    liquid::EventStoreMetadata result;
    result.session = liquid::SessionId{17};
    result.engineVersion = "0.1.0-test";
    return result;
}

liquid::Value entry(std::string key, liquid::Value value) {
    liquid::Value::Object payload;
    payload.emplace("key", liquid::Value(std::move(key)));
    payload.emplace("value", std::move(value));
    return liquid::Value(std::move(payload));
}

liquid::Value removal(std::string key) {
    liquid::Value::Object payload;
    payload.emplace("key", liquid::Value(std::move(key)));
    return liquid::Value(std::move(payload));
}

}

TEST_CASE("test_replay") {
    using namespace liquid;

    MemoryEventStore store(metadata());
    store.append(EventData{
        EventType::TopologyChanged,
        1,
        entry("Light:office", Value("registered"))
    });
    store.append(EventData{
        EventType::ComponentMutated,
        1,
        entry("Light:office", Value(std::uint64_t{70}))
    });
    store.append(EventData{
        EventType::IntentCreated,
        1,
        entry("intent:1", Value(std::uint64_t{70}))
    });
    store.append(EventData{
        EventType::CommandIssued,
        1,
        entry("command:1", Value("pending"))
    });
    store.append(EventData{
        EventType::ObservedStateChanged,
        1,
        entry("Light:office", Value(std::uint64_t{70}))
    });
    store.append(EventData{
        EventType::IntentDestroyed,
        1,
        removal("intent:1")
    });
    store.append(EventData{
        EventType::SessionStarted,
        1,
        entry("session", Value(std::uint64_t{17}))
    });
    store.append(EventData{
        EventType::ConfigurationChanged,
        1,
        entry("feedback_mode", Value("deferred"))
    });
    store.append(EventData{
        EventType::FrameStarted,
        1,
        entry("frame:1", Value("started"))
    });
    store.append(EventData{
        EventType::ResolutionSelected,
        1,
        entry("resolution:1", Value("intent:1"))
    });
    store.append(EventData{
        EventType::CommandAttempted,
        1,
        entry("attempt:1", Value(std::uint64_t{1}))
    });
    store.append(EventData{
        EventType::ReportReceived,
        1,
        entry("report:1", Value("applied"))
    });
    store.append(EventData{
        EventType::FrameFailed,
        1,
        entry("frame:2", Value("system failed"))
    });
    store.append(EventData{
        EventType::Recovery,
        1,
        entry("recovery:1", Value("truncated tail"))
    });
    store.append(EventData{
        EventType::Retention,
        1,
        entry("retention:1", Value("records 1-5"))
    });
    store.append(EventData{
        EventType::ScriptExecuted,
        1,
        entry("script:1", Value("return true"))
    });
    store.append(EventData{
        EventType::FrameCompleted,
        1,
        entry("frame:3", Value("completed"))
    });

    ReplayProjector projector;
    SerializedWorldState projected = projector.project(
        store.metadata(), store.read_all());
    REQUIRE(projected.session == store.metadata().session);
    REQUIRE(projected.replayPosition == RecordId{17});
    REQUIRE(projected.topology.at("Light:office").as_string() == "registered");
    REQUIRE(projected.components.at("Light:office").as_unsigned_integer() == 70);
    REQUIRE(projected.intents.empty());
    REQUIRE(projected.commands.at("command:1").as_string() == "pending");
    REQUIRE(projected.observedState.at("Light:office").as_unsigned_integer() == 70);
    REQUIRE(projected.configuration.at("feedback_mode").as_string() == "deferred");
    REQUIRE(projected.sessionEvents.size() == 1);
    REQUIRE(projected.configurationEvents.size() == 1);
    REQUIRE(projected.frameEvents.size() == 3);
    REQUIRE(projected.resolutions.size() == 1);
    REQUIRE(projected.commandAttempts.size() == 1);
    REQUIRE(projected.reports.size() == 1);
    REQUIRE(projected.failures.size() == 1);
    REQUIRE(projected.recoveries.size() == 1);
    REQUIRE(projected.retentions.size() == 1);
    REQUIRE(projected.scripts.size() == 1);
    projected.handleGenerations.emplace(
        "behavior:4", Value(std::uint64_t{9}));

    const RecordId checkpointSequence =
        store.checkpoint(projector.checkpoint_payload(projected));
    store.retain_from_checkpoint(checkpointSequence);
    SerializedWorldState restored = projector.project(
        store.metadata(), store.read_all());
    REQUIRE(restored.topology == projected.topology);
    REQUIRE(restored.components == projected.components);
    REQUIRE(restored.commands == projected.commands);
    REQUIRE(restored.observedState == projected.observedState);
    REQUIRE(restored.configuration == projected.configuration);
    REQUIRE(restored.handleGenerations == projected.handleGenerations);
    REQUIRE(restored.sessionEvents == projected.sessionEvents);
    REQUIRE(restored.configurationEvents == projected.configurationEvents);
    REQUIRE(restored.frameEvents == projected.frameEvents);
    REQUIRE(restored.resolutions == projected.resolutions);
    REQUIRE(restored.commandAttempts == projected.commandAttempts);
    REQUIRE(restored.reports == projected.reports);
    REQUIRE(restored.failures == projected.failures);
    REQUIRE(restored.recoveries == projected.recoveries);
    REQUIRE(restored.scripts == projected.scripts);
    REQUIRE(restored.retentions.size() == projected.retentions.size() + 1);

    ReplayVerifier verifier;
    const auto recorded = store.read_all();
    REQUIRE(!verifier.compare(recorded, recorded).has_value());

    auto different = recorded;
    different.front().payload = Value();
    const auto divergence = verifier.compare(recorded, different);
    REQUIRE(divergence.has_value());
    REQUIRE(divergence->recordIndex == 0);
    REQUIRE(divergence->expected.has_value());
    REQUIRE(divergence->actual.has_value());

    different = recorded;
    different.pop_back();
    const auto missing = verifier.compare(recorded, different);
    REQUIRE(missing.has_value());
    REQUIRE(missing->recordIndex == different.size());
    REQUIRE(!missing->actual.has_value());

    std::size_t callbackCalls = 0;
    REQUIRE(!verifier.verify(recorded, [&](const EventRecord& evidence) {
        ++callbackCalls;
        return std::vector<EventRecord>{evidence};
    }));
    REQUIRE(callbackCalls == recorded.size());

    const RecordId divergentSequence = recorded.at(1).sequence;
    const auto callbackDivergence = verifier.verify(
        recorded,
        [divergentSequence](const EventRecord& evidence) {
            EventRecord reproduced = evidence;
            if (reproduced.sequence == divergentSequence)
                reproduced.payload = Value("different");
            return std::vector<EventRecord>{std::move(reproduced)};
        }
    );
    REQUIRE(callbackDivergence.has_value());
    REQUIRE(callbackDivergence->recordIndex == 1);

    const auto callbackFailure = verifier.verify(
        recorded,
        [divergentSequence](const EventRecord& evidence) -> std::vector<EventRecord> {
            if (evidence.sequence == divergentSequence)
                throw std::runtime_error("host execution failed");
            return {evidence};
        }
    );
    REQUIRE(callbackFailure.has_value());
    REQUIRE(callbackFailure->recordIndex == 1);
    REQUIRE(callbackFailure->reason.find("host execution failed") != std::string::npos);

    MemoryEventStore runtimeShaped(metadata());
    Value::Object issuedCommand;
    issuedCommand.emplace("key", Value("command:8"));
    issuedCommand.emplace("value", Value("pending"));
    issuedCommand.emplace("command_id", Value(std::uint64_t{8}));
    issuedCommand.emplace("route", Value("test.adapter"));
    issuedCommand.emplace("target", Value("office"));
    issuedCommand.emplace("desired", Value(std::uint64_t{70}));
    issuedCommand.emplace("issued_at", Value(std::uint64_t{100}));
    runtimeShaped.append(EventData{
        EventType::CommandIssued, 1, Value(std::move(issuedCommand))});

    Value::Object commandStatus;
    commandStatus.emplace("key", Value("command:8"));
    commandStatus.emplace("value", Value("applied"));
    commandStatus.emplace("command_id", Value(std::uint64_t{8}));
    commandStatus.emplace("status", Value("applied"));
    commandStatus.emplace("reason", Value("device report"));
    runtimeShaped.append(EventData{
        EventType::CommandStatusChanged, 1, Value(std::move(commandStatus))});

    Value::Object observed;
    observed.emplace("key", Value("test.adapter:office"));
    observed.emplace("value", Value(std::uint64_t{70}));
    observed.emplace("command_id", Value(std::uint64_t{8}));
    observed.emplace("route", Value("test.adapter"));
    observed.emplace("target", Value("office"));
    observed.emplace("observed", Value(std::uint64_t{70}));
    runtimeShaped.append(EventData{
        EventType::ObservedStateChanged, 1, Value(std::move(observed))});

    const auto runtimeProjection = projector.project(
        runtimeShaped.metadata(), runtimeShaped.read_all());
    const auto& commandState = runtimeProjection.commands.at(
        "command:8").as_object();
    REQUIRE(commandState.at("route").as_string() == "test.adapter");
    REQUIRE(commandState.at("desired").as_unsigned_integer() == 70);
    REQUIRE(commandState.at("status").as_string() == "applied");
    REQUIRE(runtimeProjection.observedState.at(
        "test.adapter:office").as_unsigned_integer() == 70);

}
