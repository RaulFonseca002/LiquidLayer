#include "liquid/Runtime.hpp"
#include "liquid/Value.hpp"
#include "liquid/effects/Feedback.hpp"
#include "liquid/events/MemoryEventStore.hpp"
#include "liquid/events/Replay.hpp"
#include "liquid/events/ValueCodec.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

liquid::EventStoreMetadata metadata() {
    liquid::EventStoreMetadata result;
    result.session = liquid::SessionId{1};
    result.engineVersion = "0.1.0-coverage";
    return result;
}

liquid::Value checkpoint_projection() {
    liquid::Value::Object projection;
    for (const char* field : {
             "topology", "components", "intents", "commands", "observed_state"}) {
        projection.emplace(field, liquid::Value(liquid::Value::Object{}));
    }
    return liquid::Value(std::move(projection));
}

liquid::EffectReport applied_report() {
    return liquid::EffectReport{
        liquid::SessionId{1},
        liquid::CommandId{1},
        liquid::AdapterRoute{"edge.adapter"},
        liquid::EffectTarget{"office"},
        liquid::CommandStatus::Applied,
        liquid::Value(std::uint64_t{70}),
        {},
        10
    };
}

liquid::EffectCommand command() {
    return liquid::EffectCommand{
        liquid::SessionId{1},
        liquid::CommandId{1},
        liquid::ResolvedEffect{
            liquid::AdapterRoute{"edge.adapter"},
            liquid::EffectTarget{"office"},
            liquid::Value(std::uint64_t{70})
        },
        10
    };
}

class EdgeAdapter final : public liquid::EffectAdapter {
public:
    enum class Behavior {
        Accept,
        Apply,
        Reject,
        Fail,
        ThrowStandard,
        ThrowUnknown
    };

private:
    liquid::AdapterRoute adapterRoute;

public:
    liquid::AdapterCapabilities adapterCapabilities{true, true, true};
    Behavior behavior = Behavior::Accept;
    std::size_t dispatches = 0;
    liquid::FeedbackSender feedback;

    explicit EdgeAdapter(std::string route = "edge.adapter")
        : adapterRoute(std::move(route)) {
    }

    const liquid::AdapterRoute& route() const override {
        return adapterRoute;
    }

    liquid::AdapterCapabilities capabilities() const override {
        return adapterCapabilities;
    }

    liquid::DispatchResult dispatch(
        const liquid::EffectCommand& effectCommand,
        liquid::FeedbackSender sender
    ) override {
        ++dispatches;
        feedback = std::move(sender);
        switch (behavior) {
        case Behavior::Accept:
            return liquid::DispatchResult::accepted();
        case Behavior::Apply:
            return liquid::DispatchResult::accepted(liquid::EffectReport{
                effectCommand.sessionId,
                effectCommand.commandId,
                effectCommand.effect.adapterRoute,
                effectCommand.effect.target,
                liquid::CommandStatus::Applied,
                effectCommand.effect.desiredValue,
                {},
                effectCommand.issuedAtMs
            });
        case Behavior::Reject:
            return liquid::DispatchResult::rejected("rejected");
        case Behavior::Fail:
            return liquid::DispatchResult::failed("failed");
        case Behavior::ThrowStandard:
            throw std::runtime_error("adapter failure");
        case Behavior::ThrowUnknown:
            throw 7;
        }
        throw std::logic_error("unknown adapter behavior");
    }
};

liquid::RuntimeOptions runtime_options(
    liquid::FeedbackTiming timing = liquid::FeedbackTiming::Immediate
) {
    liquid::RuntimeOptions options;
    options.sessionId = liquid::SessionId{1};
    options.feedbackTiming = timing;
    options.allowVolatileEffects = true;
    return options;
}

liquid::ResolvedEffect effect(
    std::uint64_t desired = 70,
    std::string route = "edge.adapter",
    std::string target = "office"
) {
    return liquid::ResolvedEffect{
        liquid::AdapterRoute{std::move(route)},
        liquid::EffectTarget{std::move(target)},
        liquid::Value{desired}
    };
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

liquid::Value entry(std::string key, liquid::Value value) {
    liquid::Value::Object payload;
    payload.emplace("key", liquid::Value(std::move(key)));
    payload.emplace("value", std::move(value));
    return liquid::Value(std::move(payload));
}

liquid::EventRecord record(
    std::uint64_t sequence,
    liquid::EventType type,
    liquid::Value payload
) {
    return liquid::EventRecord{
        liquid::RecordId{sequence}, type, 1, std::move(payload)};
}

}

TEST_CASE("Value validates UTF-8 and every bounded container edge") {
    using liquid::Value;
    using liquid::ValueLimits;

    for (const std::string& valid : {
             std::string{"ASCII"},
             std::string{"\xC2\xA2", 2},
             std::string{"\xE2\x82\xAC", 3},
             std::string{"\xF0\x9F\x98\x80", 4}}) {
        REQUIRE(Value(valid).as_string() == valid);
    }

    for (const std::string& invalid : {
             std::string{"\x80", 1},
             std::string{"\xC2", 1},
             std::string{"\xE2\x82", 2},
             std::string{"\xF0\x9F\x98", 3},
             std::string{"\xC2\x20", 2},
             std::string{"\xE0\x80\x80", 3},
             std::string{"\xF0\x80\x80\x80", 4},
             std::string{"\xF4\x90\x80\x80", 4},
             std::string{"\xED\xA0\x80", 3}}) {
        REQUIRE_THROWS_AS(Value(invalid), std::invalid_argument);
    }

    REQUIRE_THROWS_AS(
        Value(Value::Bytes(ValueLimits::maxByteStringBytes + 1)),
        std::invalid_argument);
    REQUIRE_THROWS_AS(
        Value(Value::Array(ValueLimits::maxArrayItems + 1)),
        std::invalid_argument);

    Value::Object tooLargeObject;
    for (std::size_t index = 0; index <= ValueLimits::maxObjectItems; ++index)
        tooLargeObject.emplace(std::to_string(index), Value());
    REQUIRE_THROWS_AS(Value(std::move(tooLargeObject)), std::invalid_argument);

    Value::Object invalidKey;
    invalidKey.emplace(std::string{"\x80", 1}, Value());
    REQUIRE_THROWS_AS(Value(std::move(invalidKey)), std::invalid_argument);

    Value::Array root(ValueLimits::maxArrayItems);
    for (Value& child : root)
        child = Value(Value::Array(4), Value::Unchecked{});
    Value tooManyNodes(std::move(root), Value::Unchecked{});
    REQUIRE_THROWS_AS(tooManyNodes.validate(), std::invalid_argument);
}

TEST_CASE("canonical Value decoder rejects every malformed shape") {
    using liquid::Value;
    using liquid::ValueLimits;

    const Value complete(Value::Array{
        Value(), Value(false), Value(true), Value(std::int64_t{-1}),
        Value(std::uint64_t{1}), Value(1.5), Value("text"),
        Value(Value::Bytes{1, 2}),
        Value(Value::Object{{"key", Value("value")}})
    });
    const auto encoded = liquid::encode_value(complete);
    REQUIRE(liquid::decode_value(encoded) == complete);

    for (std::size_t size = 0; size < encoded.size(); ++size) {
        const std::span<const std::uint8_t> truncated(encoded.data(), size);
        REQUIRE_THROWS_AS(liquid::decode_value(truncated), std::invalid_argument);
    }

    std::vector<std::uint8_t> oversizedString{6};
    append_u32(oversizedString,
               static_cast<std::uint32_t>(ValueLimits::maxStringBytes + 1));
    REQUIRE_THROWS_AS(liquid::decode_value(oversizedString), std::invalid_argument);

    std::vector<std::uint8_t> oversizedBytes{7};
    append_u32(oversizedBytes,
               static_cast<std::uint32_t>(ValueLimits::maxByteStringBytes + 1));
    REQUIRE_THROWS_AS(liquid::decode_value(oversizedBytes), std::invalid_argument);

    std::vector<std::uint8_t> oversizedArray{8};
    append_u32(oversizedArray,
               static_cast<std::uint32_t>(ValueLimits::maxArrayItems + 1));
    REQUIRE_THROWS_AS(liquid::decode_value(oversizedArray), std::invalid_argument);

    std::vector<std::uint8_t> oversizedObject{9};
    append_u32(oversizedObject,
               static_cast<std::uint32_t>(ValueLimits::maxObjectItems + 1));
    REQUIRE_THROWS_AS(liquid::decode_value(oversizedObject), std::invalid_argument);

    std::vector<std::uint8_t> duplicateKeys{9};
    append_u32(duplicateKeys, 2);
    for (int index = 0; index < 2; ++index) {
        append_u32(duplicateKeys, 1);
        duplicateKeys.push_back('x');
        duplicateKeys.push_back(0);
    }
    REQUIRE_THROWS_AS(liquid::decode_value(duplicateKeys), std::invalid_argument);

    std::vector<std::uint8_t> tooDeep{0};
    for (std::size_t depth = 0; depth <= ValueLimits::maxDepth; ++depth) {
        std::vector<std::uint8_t> parent{8};
        append_u32(parent, 1);
        parent.insert(parent.end(), tooDeep.begin(), tooDeep.end());
        tooDeep = std::move(parent);
    }
    REQUIRE_THROWS_AS(liquid::decode_value(tooDeep), std::invalid_argument);

    std::vector<std::uint8_t> tooManyNodes{8};
    append_u32(tooManyNodes, static_cast<std::uint32_t>(ValueLimits::maxArrayItems));
    const std::array<std::uint8_t, 9> fourNulls{8, 4, 0, 0, 0, 0, 0, 0, 0};
    for (std::size_t index = 0; index < ValueLimits::maxArrayItems; ++index)
        tooManyNodes.insert(tooManyNodes.end(), fourNulls.begin(), fourNulls.end());
    REQUIRE_THROWS_AS(liquid::decode_value(tooManyNodes), std::invalid_argument);
}

TEST_CASE("effect contracts reject malformed commands reports and dispatches") {
    using namespace std::chrono_literals;
    using namespace liquid;

    for (const std::string& route : {
             std::string{},
             std::string(EffectLimits::maxAdapterRouteBytes + 1, 'x'),
             std::string{"nul\0route", 9},
             std::string{"bad route"}}) {
        REQUIRE_THROWS_AS(AdapterRoute(route), std::invalid_argument);
    }
    REQUIRE(AdapterRoute("aZ09._-/").value() == "aZ09._-/");

    for (const std::string& target : {
             std::string{},
             std::string(EffectLimits::maxTargetBytes + 1, 'x'),
             std::string{"nul\0target", 10},
             std::string{"\x80", 1}}) {
        REQUIRE_THROWS_AS(EffectTarget(target), std::invalid_argument);
    }

    RetryPolicy policy;
    policy.retryDelays.assign(EffectLimits::maxRetryDelays + 1, 1ms);
    REQUIRE_THROWS_AS(policy.validate(), std::invalid_argument);
    policy.retryDelays.clear();
    policy.overallTimeout = 0ms;
    REQUIRE_THROWS_AS(policy.validate(), std::invalid_argument);
    policy.overallTimeout = 10ms;
    policy.retryDelays = {1ms, 1ms};
    REQUIRE_THROWS_AS(policy.validate(), std::invalid_argument);
    policy.retryDelays = {10ms};
    REQUIRE_THROWS_AS(policy.validate(), std::invalid_argument);

    EffectCommand validCommand = command();
    EffectCommand invalidCommand = validCommand;
    invalidCommand.sessionId = SessionId{};
    REQUIRE_THROWS_AS(validate_effect_command(invalidCommand), std::invalid_argument);
    invalidCommand = validCommand;
    invalidCommand.commandId = CommandId{};
    REQUIRE_THROWS_AS(validate_effect_command(invalidCommand), std::invalid_argument);
    validate_effect_command(validCommand);

    EffectReport validReport = applied_report();
    validate_effect_report(validReport);
    for (const CommandStatus status : {
             CommandStatus::Rejected, CommandStatus::Failed,
             CommandStatus::TimedOut, CommandStatus::Superseded,
             CommandStatus::Indeterminate}) {
        EffectReport report = validReport;
        report.status = status;
        report.observedValue.reset();
        validate_effect_report(report);
    }

    EffectReport invalidReport = validReport;
    invalidReport.sessionId = SessionId{};
    REQUIRE_THROWS_AS(validate_effect_report(invalidReport), std::invalid_argument);
    invalidReport = validReport;
    invalidReport.commandId = CommandId{};
    REQUIRE_THROWS_AS(validate_effect_report(invalidReport), std::invalid_argument);
    invalidReport = validReport;
    invalidReport.status = CommandStatus::Pending;
    REQUIRE_THROWS_AS(validate_effect_report(invalidReport), std::invalid_argument);
    invalidReport = validReport;
    invalidReport.observedValue.reset();
    REQUIRE_THROWS_AS(validate_effect_report(invalidReport), std::invalid_argument);
    invalidReport = validReport;
    invalidReport.status = CommandStatus::Failed;
    REQUIRE_THROWS_AS(validate_effect_report(invalidReport), std::invalid_argument);
    invalidReport = validReport;
    invalidReport.diagnostic.assign(EffectLimits::maxDiagnosticBytes + 1, 'x');
    REQUIRE_THROWS_AS(validate_effect_report(invalidReport), std::invalid_argument);
    invalidReport = validReport;
    invalidReport.diagnostic = std::string{"\x80", 1};
    REQUIRE_THROWS_AS(validate_effect_report(invalidReport), std::invalid_argument);

    DispatchResult invalidDispatch{
        DispatchDisposition::Rejected, validReport, {}};
    REQUIRE_THROWS_AS(invalidDispatch.validate(), std::invalid_argument);
    REQUIRE(DispatchResult::accepted(validReport).immediateReport == validReport);
    REQUIRE(DispatchResult::rejected("no").disposition ==
            DispatchDisposition::Rejected);
    REQUIRE(DispatchResult::failed("no").disposition ==
            DispatchDisposition::Failed);

    validate_dispatch_result_for_command(
        DispatchResult::accepted(validReport), validCommand);
    for (int mismatch = 0; mismatch < 4; ++mismatch) {
        EffectReport report = validReport;
        if (mismatch == 0)
            report.sessionId = SessionId{2};
        else if (mismatch == 1)
            report.commandId = CommandId{2};
        else if (mismatch == 2)
            report.adapterRoute = AdapterRoute{"other.adapter"};
        else
            report.target = EffectTarget{"other"};
        REQUIRE_THROWS_AS(validate_dispatch_result_for_command(
            DispatchResult::accepted(report), validCommand), std::invalid_argument);
    }
}

TEST_CASE("feedback and memory store cover empty moved and capacity states") {
    using namespace liquid;

    auto channel = make_feedback_channel(2);
    REQUIRE(!channel.sender.is_closed());
    FeedbackReceiver receiver = std::move(channel.receiver);
    REQUIRE(channel.receiver.is_shutdown());
    REQUIRE(channel.receiver.pending() == 0);
    REQUIRE(channel.receiver.drain().empty());
    REQUIRE(!channel.receiver.try_receive());
    channel.receiver.shutdown();
    FeedbackReceiver* receiverAlias = &receiver;
    receiver = std::move(*receiverAlias);
    REQUIRE(!receiver.is_shutdown());
    receiver.shutdown();
    REQUIRE(receiver.is_shutdown());

    FeedbackSender sender;
    REQUIRE(sender.is_closed());

    REQUIRE_THROWS_AS(MemoryEventStore(metadata(), 0), EventStoreError);
    REQUIRE_THROWS_AS(
        MemoryEventStore(metadata(), EventLimits::maxRecordsRead + 1),
        EventStoreError);

    for (EventStoreMetadata invalid : {
             EventStoreMetadata{SessionId{}, "engine", FeedbackTiming::Deferred, 1, 1},
             EventStoreMetadata{SessionId{1}, "", FeedbackTiming::Deferred, 1, 1},
             EventStoreMetadata{SessionId{1},
                 std::string(EventLimits::maxEngineVersionBytes + 1, 'x'),
                 FeedbackTiming::Deferred, 1, 1},
             EventStoreMetadata{SessionId{1}, std::string{"\x80", 1},
                 FeedbackTiming::Deferred, 1, 1},
             EventStoreMetadata{SessionId{1}, "engine", FeedbackTiming::Deferred, 0, 1}}) {
        REQUIRE_THROWS_AS(MemoryEventStore(std::move(invalid)), EventStoreError);
    }

    MemoryEventStore store(metadata(), 3);
    const std::vector<EventData> empty;
    REQUIRE(store.append_batch(empty).empty());
    REQUIRE_THROWS_AS(store.retain_from_checkpoint(RecordId{99}), EventStoreError);
    REQUIRE_THROWS_AS(store.append(EventData{
        static_cast<EventType>(0), 1, Value()}), EventStoreError);
    REQUIRE_THROWS_AS(store.append(EventData{
        static_cast<EventType>(999), 1, Value()}), EventStoreError);
    REQUIRE_THROWS_AS(store.append(EventData{
        EventType::FrameStarted, 2, Value()}), EventStoreError);

    MemoryEventStore full(metadata(), 1);
    full.append(EventData{EventType::FrameStarted, 1, Value()});
    REQUIRE_THROWS_AS(full.append(EventData{
        EventType::FrameCompleted, 1, Value()}), EventStoreError);

    const std::array<const char*, 5> fields{
        "topology", "components", "intents", "commands", "observed_state"};
    for (const char* omitted : fields) {
        Value::Object incomplete;
        for (const char* field : fields) {
            if (std::string(field) != omitted)
                incomplete.emplace(field, Value(Value::Object{}));
        }
        REQUIRE_THROWS_AS(store.checkpoint(Value(std::move(incomplete))),
                          EventStoreError);
    }
    REQUIRE_THROWS_AS(store.checkpoint(Value()), EventStoreError);

    MemoryEventStore retentionFull(metadata(), 1);
    const RecordId only = retentionFull.checkpoint(checkpoint_projection());
    REQUIRE_THROWS_AS(retentionFull.retain_from_checkpoint(only), EventStoreError);

    EventStoreMetadata exhaustedMetadata = metadata();
    exhaustedMetadata.fileGeneration = std::numeric_limits<std::uint64_t>::max();
    MemoryEventStore exhausted(exhaustedMetadata, 2);
    const RecordId exhaustedCheckpoint = exhausted.checkpoint(checkpoint_projection());
    REQUIRE_THROWS_AS(exhausted.retain_from_checkpoint(exhaustedCheckpoint),
                      EventStoreError);
}

TEST_CASE("replay projection covers removals merged state and rich checkpoints") {
    using namespace liquid;

    Value::Object removedTopology;
    removedTopology.emplace("key", Value("room"));
    removedTopology.emplace("removed", Value(true));

    Value::Object issued;
    issued.emplace("command_id", Value(std::uint64_t{5}));
    issued.emplace("status", Value("pending"));
    Value::Object completed;
    completed.emplace("command_id", Value(std::uint64_t{5}));
    completed.emplace("status", Value("applied"));

    Value::Object observed;
    observed.emplace("route", Value("edge.adapter"));
    observed.emplace("target", Value("office"));
    observed.emplace("observed", Value(std::uint64_t{70}));

    Value::Object generations;
    generations.emplace("behavior:0", Value(std::uint64_t{2}));
    Value::Object generationConfiguration;
    generationConfiguration.emplace("key", Value("handle_generations"));
    generationConfiguration.emplace("value", Value(generations));

    const std::vector<EventRecord> records{
        record(1, EventType::TopologyChanged, entry("room", Value("live"))),
        record(2, EventType::TopologyChanged, Value(std::move(removedTopology))),
        record(3, EventType::ComponentMutated,
               entry("light", Value(std::uint64_t{70}))),
        record(4, EventType::ComponentRemoved, entry("light", Value())),
        record(5, EventType::IntentCreated,
               entry("intent", Value(std::uint64_t{70}))),
        record(6, EventType::IntentDestroyed, entry("intent", Value())),
        record(7, EventType::CommandIssued, Value(std::move(issued))),
        record(8, EventType::CommandStatusChanged, Value(std::move(completed))),
        record(9, EventType::ObservedStateChanged, Value(std::move(observed))),
        record(10, EventType::ConfigurationChanged,
               Value(std::move(generationConfiguration)))
    };

    ReplayProjector projector;
    SerializedWorldState state = projector.project(metadata(), records);
    REQUIRE(state.topology.empty());
    REQUIRE(state.components.empty());
    REQUIRE(state.intents.empty());
    REQUIRE(state.commands.at("command:5").as_object().at("status").as_string() ==
            "applied");
    REQUIRE(state.observedState.at("edge.adapter:office").as_unsigned_integer() == 70);
    REQUIRE(state.observedAuthority.contains("edge.adapter:office"));
    REQUIRE(state.handleGenerations == generations);

    const Value checkpoint = projector.checkpoint_payload(state);
    const auto restored = projector.project(metadata(), std::array{
        record(11, EventType::Checkpoint, checkpoint)});
    REQUIRE(restored.commands == state.commands);
    REQUIRE(restored.observedAuthority == state.observedAuthority);

    SerializedWorldState invalidState;
    REQUIRE_THROWS_AS(projector.checkpoint_payload(invalidState), EventStoreError);
}

TEST_CASE("replay rejects malformed records and checkpoint fields") {
    using namespace liquid;

    ReplayProjector projector;
    const auto expect_project_error = [&](std::vector<EventRecord> records) {
        REQUIRE_THROWS_AS(projector.project(metadata(), records), EventStoreError);
    };

    expect_project_error({record(0, EventType::SessionStarted, Value())});
    expect_project_error({
        record(1, EventType::SessionStarted, Value()),
        record(3, EventType::SessionStarted, Value())});
    EventRecord future = record(1, EventType::SessionStarted, Value());
    future.version = 2;
    expect_project_error({future});
    expect_project_error({
        record(std::numeric_limits<std::uint64_t>::max(),
               EventType::SessionStarted, Value()),
        record(1, EventType::SessionStarted, Value())});

    expect_project_error({record(1, EventType::ComponentMutated, Value())});
    expect_project_error({record(1, EventType::ComponentMutated,
                                 Value(Value::Object{}))});
    expect_project_error({record(1, EventType::ComponentMutated,
                                 entry("key", Value()).as_object().at("value"))});
    Value::Object nonStringKey;
    nonStringKey.emplace("key", Value(std::uint64_t{1}));
    nonStringKey.emplace("value", Value());
    expect_project_error({record(1, EventType::ComponentMutated,
                                 Value(std::move(nonStringKey))) });

    Value::Object badObserved;
    badObserved.emplace("route", Value(std::uint64_t{1}));
    badObserved.emplace("target", Value("office"));
    badObserved.emplace("observed", Value(std::uint64_t{70}));
    expect_project_error({record(1, EventType::ObservedStateChanged,
                                 Value(std::move(badObserved))) });

    Value::Object badGenerations;
    badGenerations.emplace("key", Value("handle_generations"));
    badGenerations.emplace("value", Value("not-object"));
    expect_project_error({record(1, EventType::ConfigurationChanged,
                                 Value(std::move(badGenerations))) });

    const std::array<const char*, 5> required{
        "topology", "components", "intents", "commands", "observed_state"};
    for (const char* field : required) {
        Value::Object checkpoint = checkpoint_projection().as_object();
        checkpoint.erase(field);
        expect_project_error({record(1, EventType::Checkpoint,
                                     Value(std::move(checkpoint))) });

        checkpoint = checkpoint_projection().as_object();
        checkpoint.insert_or_assign(field, Value("not-object"));
        expect_project_error({record(1, EventType::Checkpoint,
                                     Value(std::move(checkpoint))) });
    }

    for (const char* field : {
             "observed_authority", "configuration", "handle_generations"}) {
        Value::Object checkpoint = checkpoint_projection().as_object();
        checkpoint.emplace(field, Value("not-object"));
        expect_project_error({record(1, EventType::Checkpoint,
                                     Value(std::move(checkpoint))) });
    }

    Value::Object wrongSession = checkpoint_projection().as_object();
    wrongSession.emplace("session", Value(std::uint64_t{2}));
    expect_project_error({record(1, EventType::Checkpoint,
                                 Value(std::move(wrongSession))) });
    Value::Object typedSession = checkpoint_projection().as_object();
    typedSession.emplace("session", Value("wrong-type"));
    expect_project_error({record(1, EventType::Checkpoint,
                                 Value(std::move(typedSession))) });
    Value::Object typedPosition = checkpoint_projection().as_object();
    typedPosition.emplace("replay_position", Value("wrong-type"));
    expect_project_error({record(1, EventType::Checkpoint,
                                 Value(std::move(typedPosition))) });

    for (const char* field : {
             "session_events", "configuration_events", "frame_events",
             "resolutions", "command_attempts", "reports", "failures",
             "recoveries", "retentions", "scripts"}) {
        Value::Object checkpoint = checkpoint_projection().as_object();
        checkpoint.emplace(field, Value("not-array"));
        expect_project_error({record(1, EventType::Checkpoint,
                                     Value(std::move(checkpoint))) });
    }

    const auto expect_checkpoint_event_error = [&](Value encodedEvent) {
        Value::Object checkpoint = checkpoint_projection().as_object();
        checkpoint.emplace("frame_events",
                           Value(Value::Array{std::move(encodedEvent)}));
        expect_project_error({record(1, EventType::Checkpoint,
                                     Value(std::move(checkpoint))) });
    };

    expect_checkpoint_event_error(Value());
    expect_checkpoint_event_error(Value(Value::Object{}));
    for (const std::uint64_t type : {std::uint64_t{0}, std::uint64_t{999}}) {
        Value::Object event;
        event.emplace("type", Value(type));
        event.emplace("version", Value(std::uint64_t{1}));
        event.emplace("sequence", Value(std::uint64_t{1}));
        event.emplace("payload", Value());
        expect_checkpoint_event_error(Value(std::move(event)));
    }
    Value::Object wrongVersionType;
    wrongVersionType.emplace("type", Value(std::uint64_t{1}));
    wrongVersionType.emplace("version", Value("one"));
    wrongVersionType.emplace("sequence", Value(std::uint64_t{1}));
    wrongVersionType.emplace("payload", Value());
    expect_checkpoint_event_error(Value(std::move(wrongVersionType)));

    Value::Object futureVersion;
    futureVersion.emplace("type", Value(std::uint64_t{1}));
    futureVersion.emplace("version", Value(std::uint64_t{2}));
    futureVersion.emplace("sequence", Value(std::uint64_t{1}));
    futureVersion.emplace("payload", Value());
    expect_checkpoint_event_error(Value(std::move(futureVersion)));

    Value::Object zeroSequence;
    zeroSequence.emplace("type", Value(std::uint64_t{1}));
    zeroSequence.emplace("version", Value(std::uint64_t{1}));
    zeroSequence.emplace("sequence", Value(std::uint64_t{0}));
    zeroSequence.emplace("payload", Value());
    expect_checkpoint_event_error(Value(std::move(zeroSequence)));

    Value::Object missingPayload;
    missingPayload.emplace("type", Value(std::uint64_t{1}));
    missingPayload.emplace("version", Value(std::uint64_t{1}));
    missingPayload.emplace("sequence", Value(std::uint64_t{1}));
    expect_checkpoint_event_error(Value(std::move(missingPayload)));
}

TEST_CASE("replay verifier reports every completion and exception outcome") {
    using namespace liquid;

    const std::vector<EventRecord> evidence{
        record(1, EventType::SessionStarted, Value()),
        record(2, EventType::FrameCompleted, Value())
    };
    ReplayVerifier verifier;

    const auto extra = verifier.compare(
        std::span<const EventRecord>{}, evidence);
    REQUIRE((extra && extra->reason.find("unexpected") != std::string::npos));

    const auto emptyCallback = verifier.verify(evidence, {});
    REQUIRE((emptyCallback && emptyCallback->recordIndex == 0));
    const auto emptyCallbackNoEvidence = verifier.verify({}, {});
    REQUIRE((emptyCallbackNoEvidence && !emptyCallbackNoEvidence->expected));

    const auto unexpected = verifier.verify(evidence,
        [&](const EventRecord& input) {
            return input.sequence == RecordId{2}
                ? std::vector<EventRecord>{input, input}
                : std::vector<EventRecord>{input};
        });
    REQUIRE((unexpected &&
             unexpected->reason.find("unexpected") != std::string::npos));

    const auto early = verifier.verify(evidence,
        [](const EventRecord&) { return std::vector<EventRecord>{}; });
    REQUIRE((early && early->reason.find("ended early") != std::string::npos));

    const auto unknownFailure = verifier.verify(evidence,
        [](const EventRecord&) -> std::vector<EventRecord> { throw 7; });
    REQUIRE((unknownFailure &&
             unknownFailure->reason.find("unknown exception") != std::string::npos));
}

TEST_CASE("Runtime effect configuration rejects invalid boundaries") {
    using namespace liquid;

    RuntimeOptions invalidDefaults;
    REQUIRE_THROWS_AS(Runtime(invalidDefaults), std::invalid_argument);

    RuntimeOptions noStore;
    noStore.sessionId = SessionId{1};
    REQUIRE_THROWS_AS(Runtime(noStore), std::invalid_argument);

    RuntimeOptions noCommands = runtime_options();
    noCommands.maxRetainedCommands = 0;
    REQUIRE_THROWS_AS(Runtime(noCommands), std::invalid_argument);

    RuntimeOptions noFeedback = runtime_options();
    noFeedback.feedbackCapacity = 0;
    REQUIRE_THROWS_AS(Runtime(noFeedback), std::invalid_argument);

    EventStoreMetadata mismatched = metadata();
    mismatched.session = SessionId{2};
    MemoryEventStore mismatchedStore(mismatched);
    RuntimeOptions mismatchedOptions = runtime_options();
    mismatchedOptions.eventStore = &mismatchedStore;
    REQUIRE_THROWS_AS(Runtime(mismatchedOptions), std::invalid_argument);

    EventStoreMetadata timingMetadata = metadata();
    timingMetadata.feedbackTiming = FeedbackTiming::Deferred;
    MemoryEventStore timingStore(timingMetadata);
    RuntimeOptions timingOptions = runtime_options(FeedbackTiming::Immediate);
    timingOptions.eventStore = &timingStore;
    REQUIRE_THROWS_AS(Runtime(timingOptions), std::invalid_argument);

    Runtime plain;
    REQUIRE_THROWS_AS(plain.run_frame(FrameInput{}), std::logic_error);
    REQUIRE_THROWS_AS(plain.register_adapter(
        std::shared_ptr<EffectAdapter>{}), std::logic_error);
    REQUIRE_THROWS_AS(plain.feedback_sender(), std::logic_error);
    REQUIRE_THROWS_AS(plain.observed_state(
        AdapterRoute{"edge.adapter"}, EffectTarget{"office"}), std::logic_error);
    REQUIRE_THROWS_AS(plain.command_status(CommandId{1}), std::logic_error);
    REQUIRE_THROWS_AS(plain.reconcile_indeterminate(
        AdapterRoute{"edge.adapter"}, EffectTarget{"office"},
        Value{std::uint64_t{1}}),
        std::logic_error);
    REQUIRE_THROWS_AS(plain.checkpoint(), std::logic_error);

    Runtime configured(runtime_options());
    REQUIRE_THROWS_AS(configured.register_adapter(
        std::shared_ptr<EffectAdapter>{}), std::invalid_argument);
    auto adapter = std::make_shared<EdgeAdapter>();
    configured.register_adapter(adapter);
    REQUIRE_THROWS_AS(configured.register_adapter(adapter), std::invalid_argument);
    REQUIRE_THROWS_AS(configured.run_frame(FrameInput{0, {}, {effect(1, "missing")}}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(configured.run_frame(FrameInput{
        0, {}, {effect(1), effect(2)}}), std::invalid_argument);
    REQUIRE_THROWS_AS(configured.reconcile_indeterminate(
        adapter->route(), EffectTarget{"office"}, Value{std::uint64_t{1}}),
        std::logic_error);
    REQUIRE(!configured.observed_state(adapter->route(), EffectTarget{"office"}));
    REQUIRE(!configured.command_status(CommandId{99}));
}

TEST_CASE("Runtime dispatch covers accepted rejected failed and exception outcomes") {
    using namespace liquid;

    const auto run = [](EdgeAdapter::Behavior behavior,
                        AdapterCapabilities capabilities) {
        Runtime runtime(runtime_options());
        auto adapter = std::make_shared<EdgeAdapter>();
        adapter->behavior = behavior;
        adapter->adapterCapabilities = capabilities;
        runtime.register_adapter(adapter);
        const FrameResult frame = runtime.run_frame(FrameInput{0, {}, {effect()}});
        REQUIRE(frame.commands.size() == 1);
        return std::pair{runtime.command_status(frame.commands.front().commandId),
                         adapter->dispatches};
    };

    const auto rejected = run(
        EdgeAdapter::Behavior::Reject, AdapterCapabilities{true, true, true});
    REQUIRE(rejected.first == CommandStatus::Rejected);
    const auto failed = run(
        EdgeAdapter::Behavior::Fail, AdapterCapabilities{true, true, true});
    REQUIRE(failed.first == CommandStatus::Failed);
    const auto standardSafe = run(
        EdgeAdapter::Behavior::ThrowStandard,
        AdapterCapabilities{true, true, false});
    REQUIRE(standardSafe.first == CommandStatus::Pending);
    const auto standardUnsafe = run(
        EdgeAdapter::Behavior::ThrowStandard,
        AdapterCapabilities{false, true, false});
    REQUIRE(standardUnsafe.first == CommandStatus::Indeterminate);
    const auto unknownSafe = run(
        EdgeAdapter::Behavior::ThrowUnknown,
        AdapterCapabilities{true, true, false});
    REQUIRE(unknownSafe.first == CommandStatus::Pending);
    const auto unknownUnsafe = run(
        EdgeAdapter::Behavior::ThrowUnknown,
        AdapterCapabilities{false, false, false});
    REQUIRE(unknownUnsafe.first == CommandStatus::Indeterminate);

    Runtime immediate(runtime_options());
    auto immediateAdapter = std::make_shared<EdgeAdapter>();
    immediateAdapter->behavior = EdgeAdapter::Behavior::Apply;
    immediate.register_adapter(immediateAdapter);
    const auto immediateFrame = immediate.run_frame(FrameInput{0, {}, {effect()}});
    REQUIRE(immediateFrame.reports.size() == 1);
    REQUIRE(immediate.observed_state(
        immediateAdapter->route(), EffectTarget{"office"}) ==
        Value{std::uint64_t{70}});

    Runtime deferred(runtime_options(FeedbackTiming::Deferred));
    auto deferredAdapter = std::make_shared<EdgeAdapter>();
    deferredAdapter->behavior = EdgeAdapter::Behavior::Apply;
    deferred.register_adapter(deferredAdapter);
    REQUIRE(deferred.run_frame(FrameInput{0, {}, {effect()}}).reports.empty());
    REQUIRE(deferred.run_frame(FrameInput{1, {}, {effect()}}).reports.size() == 1);
}

TEST_CASE("Runtime retries timeouts supersession and report rejection are explicit") {
    using namespace liquid;

    Runtime runtime(runtime_options());
    auto adapter = std::make_shared<EdgeAdapter>();
    runtime.register_adapter(adapter);
    const FrameResult first = runtime.run_frame(FrameInput{0, {}, {effect(70)}});
    const CommandId firstId = first.commands.front().commandId;
    REQUIRE(adapter->dispatches == 1);
    REQUIRE(runtime.run_frame(FrameInput{249, {}, {effect(70)}}).commands.empty());
    REQUIRE(adapter->dispatches == 1);
    REQUIRE(runtime.run_frame(FrameInput{250, {}, {effect(70)}}).commands.empty());
    REQUIRE(adapter->dispatches == 2);

    const FrameResult superseding = runtime.run_frame(
        FrameInput{251, {}, {effect(30)}});
    REQUIRE(superseding.commands.size() == 1);
    REQUIRE(runtime.command_status(firstId) == CommandStatus::Superseded);

    EffectReport staleRejected{
        SessionId{1}, firstId, adapter->route(), EffectTarget{"office"},
        CommandStatus::Rejected, std::nullopt, "late", 252};
    REQUIRE(adapter->feedback.try_send(staleRejected) == FeedbackSendResult::Sent);
    REQUIRE(runtime.run_frame(FrameInput{252, {}, {effect(30)}}).reports.empty());

    EffectReport staleApplied{
        SessionId{1}, firstId, adapter->route(), EffectTarget{"office"},
        CommandStatus::Applied, Value{std::uint64_t{70}}, {}, 253};
    REQUIRE(adapter->feedback.try_send(staleApplied) == FeedbackSendResult::Sent);
    REQUIRE(runtime.run_frame(FrameInput{253, {}, {effect(30)}}).reports.size() == 1);

    EffectReport duplicate = staleApplied;
    REQUIRE(adapter->feedback.try_send(duplicate) == FeedbackSendResult::Sent);
    REQUIRE(runtime.run_frame(FrameInput{254, {}, {effect(30)}}).reports.empty());
    duplicate.observedValue = Value{std::uint64_t{80}};
    REQUIRE(adapter->feedback.try_send(duplicate) == FeedbackSendResult::Sent);
    REQUIRE(runtime.run_frame(FrameInput{255, {}, {effect(30)}}).reports.empty());

    EffectReport unknown{
        SessionId{1}, CommandId{999}, adapter->route(), EffectTarget{"office"},
        CommandStatus::Applied, Value{std::uint64_t{1}}, {}, 256};
    REQUIRE(adapter->feedback.try_send(unknown) == FeedbackSendResult::Sent);
    EffectReport crossSession = unknown;
    crossSession.sessionId = SessionId{2};
    REQUIRE(adapter->feedback.try_send(crossSession) == FeedbackSendResult::Sent);
    EffectReport mismatched = unknown;
    mismatched.commandId = superseding.commands.front().commandId;
    mismatched.target = EffectTarget{"other"};
    REQUIRE(adapter->feedback.try_send(mismatched) == FeedbackSendResult::Sent);
    REQUIRE(runtime.run_frame(FrameInput{256, {}, {effect(30)}}).reports.empty());

    Runtime timeoutRuntime(runtime_options());
    auto timeoutAdapter = std::make_shared<EdgeAdapter>();
    timeoutRuntime.register_adapter(timeoutAdapter);
    const CommandId timeoutId = timeoutRuntime.run_frame(
        FrameInput{0, {}, {effect()}}).commands.front().commandId;
    timeoutRuntime.run_frame(FrameInput{30'000, {}, {effect()}});
    REQUIRE(timeoutRuntime.command_status(timeoutId) == CommandStatus::TimedOut);
}

TEST_CASE("Runtime command retention and restart reconciliation stay bounded") {
    using namespace liquid;

    RuntimeOptions boundedOptions = runtime_options();
    boundedOptions.maxRetainedCommands = 1;
    Runtime bounded(boundedOptions);
    auto boundedAdapter = std::make_shared<EdgeAdapter>();
    bounded.register_adapter(boundedAdapter);
    bounded.run_frame(FrameInput{0, {}, {effect(1, "edge.adapter", "one")}});
    REQUIRE_THROWS_AS(bounded.run_frame(
        FrameInput{1, {}, {effect(2, "edge.adapter", "two")}}), EventStoreError);
    REQUIRE(bounded.faulted());

    EventStoreMetadata persistentMetadata = metadata();
    persistentMetadata.feedbackTiming = FeedbackTiming::Immediate;
    MemoryEventStore store(persistentMetadata);
    RuntimeOptions firstOptions = runtime_options();
    firstOptions.eventStore = &store;
    Runtime first(firstOptions);
    auto firstAdapter = std::make_shared<EdgeAdapter>();
    firstAdapter->adapterCapabilities = {false, true, false};
    first.register_adapter(firstAdapter);
    const CommandId pending = first.run_frame(
        FrameInput{0, {}, {effect()}}).commands.front().commandId;
    REQUIRE(first.command_status(pending) == CommandStatus::Pending);

    RuntimeOptions reopenedOptions = runtime_options();
    reopenedOptions.eventStore = &store;
    Runtime reopened(reopenedOptions);
    auto reopenedAdapter = std::make_shared<EdgeAdapter>();
    reopenedAdapter->adapterCapabilities = {false, true, false};
    reopened.register_adapter(reopenedAdapter);
    REQUIRE(reopened.command_status(pending) == CommandStatus::Indeterminate);
    reopened.reconcile_indeterminate(
        reopenedAdapter->route(), EffectTarget{"office"},
        Value{std::uint64_t{70}});
    REQUIRE(reopened.command_status(pending) == CommandStatus::Applied);
    REQUIRE(reopened.checkpoint().valid());
}

TEST_CASE("Runtime restores every persisted command status") {
    using namespace liquid;

    EventStoreMetadata storeMetadata = metadata();
    storeMetadata.feedbackTiming = FeedbackTiming::Immediate;
    MemoryEventStore store(storeMetadata);

    const std::array<std::pair<const char*, CommandStatus>, 7> statuses{{
        {"pending", CommandStatus::Pending},
        {"applied", CommandStatus::Applied},
        {"rejected", CommandStatus::Rejected},
        {"failed", CommandStatus::Failed},
        {"timed-out", CommandStatus::TimedOut},
        {"superseded", CommandStatus::Superseded},
        {"indeterminate", CommandStatus::Indeterminate}
    }};
    std::uint64_t id = 1;
    for (const auto& [name, status] : statuses) {
        static_cast<void>(status);
        Value::Object commandPayload;
        commandPayload.emplace("command_id", Value(id));
        commandPayload.emplace("route", Value("edge.adapter"));
        commandPayload.emplace("target", Value(std::to_string(id)));
        commandPayload.emplace("desired", Value(id));
        commandPayload.emplace("issued_at", Value(std::uint64_t{0}));
        commandPayload.emplace("status", Value(name));
        store.append(EventData{
            EventType::CommandIssued, 1, Value(std::move(commandPayload))});
        ++id;
    }

    RuntimeOptions options = runtime_options();
    options.eventStore = &store;
    Runtime restored(options);
    id = 1;
    for (const auto& [name, status] : statuses) {
        static_cast<void>(name);
        REQUIRE(restored.command_status(CommandId{id}) == status);
        ++id;
    }
}

TEST_CASE("Runtime rejects malformed persisted command records") {
    using namespace liquid;

    const auto expect_restore_error = [](Value payload) {
        EventStoreMetadata storeMetadata = metadata();
        storeMetadata.feedbackTiming = FeedbackTiming::Immediate;
        MemoryEventStore store(storeMetadata);
        store.append(EventData{EventType::CommandIssued, 1, std::move(payload)});
        RuntimeOptions options = runtime_options();
        options.eventStore = &store;
        REQUIRE_THROWS_AS(Runtime(options), EventStoreError);
    };

    expect_restore_error(Value(Value::Object{}));
    Value::Object typedId;
    typedId.emplace("command_id", Value("not-unsigned"));
    expect_restore_error(Value(std::move(typedId)));

    Value::Object typedRoute;
    typedRoute.emplace("command_id", Value(std::uint64_t{1}));
    typedRoute.emplace("route", Value(std::uint64_t{1}));
    expect_restore_error(Value(std::move(typedRoute)));

    Value::Object invalidStatus;
    invalidStatus.emplace("command_id", Value(std::uint64_t{1}));
    invalidStatus.emplace("route", Value("edge.adapter"));
    invalidStatus.emplace("target", Value("office"));
    invalidStatus.emplace("desired", Value(std::uint64_t{70}));
    invalidStatus.emplace("issued_at", Value(std::uint64_t{0}));
    invalidStatus.emplace("status", Value("unknown"));
    expect_restore_error(Value(std::move(invalidStatus)));
}
