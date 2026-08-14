#include "liquid/Runtime.hpp"
#include "liquid/events/MemoryEventStore.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace liquid;

namespace {

class TestAdapter final : public liquid::EffectAdapter {
    liquid::AdapterRoute adapterRoute{"test.light"};

public:
    liquid::AdapterCapabilities adapterCapabilities{
        true,
        true,
        true
    };
    bool reportSynchronously = true;
    bool throwOnDispatch = false;
    std::optional<liquid::DispatchDisposition> disposition;
    std::vector<liquid::EffectCommand> dispatched;
    liquid::FeedbackSender sender;

    const liquid::AdapterRoute& route() const override {
        return adapterRoute;
    }

    liquid::AdapterCapabilities capabilities() const override {
        return adapterCapabilities;
    }

    liquid::DispatchResult dispatch(
        const liquid::EffectCommand& command,
        liquid::FeedbackSender feedback
    ) override {
        dispatched.push_back(command);
        sender = std::move(feedback);
        if (throwOnDispatch)
            throw std::runtime_error("simulated dispatch uncertainty");
        if (disposition == liquid::DispatchDisposition::Rejected)
            return liquid::DispatchResult::rejected("simulated rejection");
        if (disposition == liquid::DispatchDisposition::Failed)
            return liquid::DispatchResult::failed("simulated failure");
        if (!reportSynchronously)
            return liquid::DispatchResult::accepted();

        return liquid::DispatchResult::accepted(liquid::EffectReport{
            command.sessionId,
            command.commandId,
            command.effect.adapterRoute,
            command.effect.target,
            liquid::CommandStatus::Applied,
            command.effect.desiredValue,
            {},
            command.issuedAtMs
        });
    }
};

class FailingStore final : public liquid::EventStore {
    liquid::EventStoreMetadata storeMetadata;
    std::size_t appends = 0;
    std::size_t failAt;

public:
    explicit FailingStore(std::size_t failureAppend) : failAt(failureAppend) {
        storeMetadata.session = liquid::SessionId{42};
        storeMetadata.engineVersion = "0.1.0";
        storeMetadata.feedbackTiming = liquid::FeedbackTiming::Immediate;
    }

    const liquid::EventStoreMetadata& metadata() const override {
        return storeMetadata;
    }

    liquid::RecordId append(
        liquid::EventData,
        liquid::Durability = liquid::Durability::Durable
    ) override {
        ++appends;
        if (appends == failAt)
            throw liquid::EventStoreError("injected append failure");
        return liquid::RecordId{appends};
    }

    std::vector<liquid::RecordId> append_batch(
        std::span<const liquid::EventData> events,
        liquid::Durability durability = liquid::Durability::Durable
    ) override {
        std::vector<liquid::RecordId> result;
        for (const auto& event : events)
            result.push_back(append(event, durability));
        return result;
    }

    std::vector<liquid::EventRecord> read_all() const override { return {}; }
    void flush() override {}
    void retain_from_checkpoint(liquid::RecordId) override {}
};

struct MutatingThrowingSystem final : liquid::System {
    static constexpr std::string_view stableName =
        "tests.runtime.effects.MutatingThrowingSystem";
    static constexpr std::uint32_t version = 1;
    liquid::ComponentType<std::uint64_t> type;
    liquid::BehaviorId behavior;

    void run(liquid::World& world, liquid::FrameNumber, liquid::IntentTime) override {
        world.replace_component(type, behavior, "level", std::uint64_t{80});
        throw std::runtime_error("mutation then failure");
    }
};

struct ObservedLevelSystem final : liquid::System {
    static constexpr std::string_view stableName =
        "tests.runtime.effects.ObservedLevelSystem";
    static constexpr std::uint32_t version = 1;
    liquid::ComponentType<std::uint64_t> type;
    liquid::BehaviorId behavior;
    std::vector<std::uint64_t>* levels = nullptr;

    ObservedLevelSystem(
        liquid::ComponentType<std::uint64_t> componentType,
        liquid::BehaviorId owner,
        std::vector<std::uint64_t>* observedLevels)
        : type(componentType), behavior(owner), levels(observedLevels) {
    }

    void run(liquid::World& world, liquid::FrameNumber, liquid::IntentTime) override {
        levels->push_back(*world.read_component(type, behavior, "level"));
    }
};

liquid::ComponentCodec<std::uint64_t> unsigned_codec() {
    return {
        [](const std::uint64_t value) { return liquid::Value{value}; },
        [](const liquid::Value& value) { return value.as_unsigned_integer(); }
    };
}

liquid::ResolvedEffect light(std::uint64_t level) {
    return liquid::ResolvedEffect{
        liquid::AdapterRoute{"test.light"},
        liquid::EffectTarget{"office"},
        liquid::Value{level}
    };
}

liquid::EffectReport outcome_report(
    const liquid::EffectCommand& command,
    liquid::CommandStatus status,
    std::optional<liquid::Value> observed = std::nullopt,
    std::uint64_t reportedAt = 0
) {
    return liquid::EffectReport{
        command.sessionId,
        command.commandId,
        command.effect.adapterRoute,
        command.effect.target,
        status,
        std::move(observed),
        {},
        reportedAt
    };
}

liquid::RuntimeOptions options(liquid::FeedbackTiming timing) {
    liquid::RuntimeOptions result;
    result.sessionId = liquid::SessionId{42};
    result.feedbackTiming = timing;
    result.allowVolatileEffects = true;
    return result;
}

liquid::EffectCodec<std::uint64_t> light_effect_codec() {
    return {
        liquid::AdapterRoute{"test.light"},
        [](const liquid::ComponentName& name, std::uint64_t desired)
            -> std::optional<liquid::ResolvedEffect> {
            return liquid::ResolvedEffect{
                liquid::AdapterRoute{"test.light"},
                liquid::EffectTarget{name},
                liquid::Value{desired}};
        },
        [](const liquid::Value& observed) {
            return observed.as_unsigned_integer();
        }
    };
}

}

TEST_CASE("selected intents drive deferred effects and confirmed components") {
    Runtime runtime{options(FeedbackTiming::Deferred)};
    auto adapter = std::make_shared<TestAdapter>();
    runtime.register_adapter(adapter);

    World& world = runtime.world();
    const auto levelType = world.register_component<std::uint64_t>(
        "tests.Level", 1, unsigned_codec());
    world.register_effect_codec(levelType, light_effect_codec());
    world.add_component(levelType, "level", std::uint64_t{10});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(
        levelType, behavior, "level", ComponentAccessMode::ReadWrite);
    const ComponentSlotId slot =
        world.get_components(levelType, behavior).at("level");
    runtime.bind_effect_component(
        levelType, "level", EffectTarget{"level"});

    std::vector<std::uint64_t> observedByInput;
    world.register_system<ObservedLevelSystem>(
        Signature{}, SystemPhase::Input,
        levelType, behavior, &observedByInput);
    world.create_intent(
        behavior, levelType, slot, IntentLifetime::persistent(),
        std::uint64_t{70}, IntentPriority::High);

    const FrameResult issued = runtime.run_frame(FrameInput{100, {}, {}});
    REQUIRE(issued.commands.size() == 1);
    REQUIRE(issued.commands.front().effect.desiredValue == Value{std::uint64_t{70}});
    REQUIRE(*world.read_component(levelType, behavior, "level") == 10);
    REQUIRE(observedByInput == std::vector<std::uint64_t>{10});

    const FrameResult confirmed = runtime.run_frame(FrameInput{105, {}, {}});
    REQUIRE(confirmed.reports.size() == 1);
    REQUIRE(confirmed.commands.empty());
    REQUIRE(*world.read_component(levelType, behavior, "level") == 70);
    REQUIRE(observedByInput == std::vector<std::uint64_t>{10, 70});
}

TEST_CASE("external observations project before input systems by revision") {
    Runtime runtime{options(FeedbackTiming::Deferred)};
    auto adapter = std::make_shared<TestAdapter>();
    runtime.register_adapter(adapter);

    World& world = runtime.world();
    const auto levelType = world.register_component<std::uint64_t>(
        "tests.Level", 1, unsigned_codec());
    world.register_effect_codec(levelType, light_effect_codec());
    world.add_component(levelType, "level", std::uint64_t{10});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(
        levelType, behavior, "level", ComponentAccessMode::Read);
    runtime.bind_effect_component(
        levelType, "level", EffectTarget{"level"});

    std::vector<std::uint64_t> observedByInput;
    world.register_system<ObservedLevelSystem>(
        Signature{}, SystemPhase::Input,
        levelType, behavior, &observedByInput);
    FeedbackSender feedback = runtime.feedback_sender();
    REQUIRE(feedback.try_send(ExternalObservation{
        SessionId{42}, AdapterRoute{"test.light"}, EffectTarget{"level"},
        Value{std::uint64_t{25}}, StateRevision{2}, 100}) ==
        FeedbackSendResult::Sent);

    const FrameResult accepted = runtime.run_frame(FrameInput{100, {}, {}});
    REQUIRE(accepted.observations.size() == 1);
    REQUIRE(observedByInput == std::vector<std::uint64_t>{25});
    REQUIRE(*world.read_component(levelType, behavior, "level") == 25);

    REQUIRE(feedback.try_send(ExternalObservation{
        SessionId{42}, AdapterRoute{"test.light"}, EffectTarget{"level"},
        Value{std::uint64_t{15}}, StateRevision{1}, 101}) ==
        FeedbackSendResult::Sent);
    const FrameResult stale = runtime.run_frame(FrameInput{101, {}, {}});
    REQUIRE(stale.observations.empty());
    REQUIRE(observedByInput == std::vector<std::uint64_t>{25, 25});

    REQUIRE(feedback.try_send(ExternalObservation{
        SessionId{42}, AdapterRoute{"test.light"}, EffectTarget{"level"},
        Value{std::uint64_t{30}}, StateRevision{2}, 102}) ==
        FeedbackSendResult::Sent);
    const FrameResult conflict = runtime.run_frame(FrameInput{102, {}, {}});
    REQUIRE(conflict.observations.empty());
    REQUIRE(*world.read_component(levelType, behavior, "level") == 25);
}

TEST_CASE("test_runtime_effects") {
    {
        liquid::EventStoreMetadata metadata;
        metadata.session = liquid::SessionId{42};
        metadata.engineVersion = "0.1.0";
        metadata.feedbackTiming = liquid::FeedbackTiming::Immediate;
        liquid::MemoryEventStore store{metadata};
        auto firstOptions = options(liquid::FeedbackTiming::Immediate);
        firstOptions.eventStore = &store;
        Runtime first{firstOptions};
        TestAdapter firstAdapter;
        firstAdapter.reportSynchronously = false;
        first.register_adapter(firstAdapter);
        const auto firstId = first.run_frame(
            liquid::FrameInput{0, {}, {light(70)}}).commands.front().commandId;

        auto reopenedOptions = options(liquid::FeedbackTiming::Immediate);
        reopenedOptions.eventStore = &store;
        Runtime reopened{reopenedOptions};
        TestAdapter reopenedAdapter;
        reopened.register_adapter(reopenedAdapter);
        const auto secondId = reopened.run_frame(
            liquid::FrameInput{1, {}, {light(30)}}).commands.front().commandId;
        REQUIRE(secondId.value > firstId.value);
    }

    {
        Runtime runtime{options(liquid::FeedbackTiming::Immediate)};
        TestAdapter adapter;
        runtime.register_adapter(adapter);

        auto first = runtime.run_frame(liquid::FrameInput{10, {}, {light(70)}});
        REQUIRE(first.frame.completed);
        REQUIRE(first.commands.size() == 1);
        REQUIRE(runtime.observed_state(
            liquid::AdapterRoute{"test.light"}, liquid::EffectTarget{"office"}) ==
            std::optional<liquid::Value>{liquid::Value{std::uint64_t{70}}});

        auto unchanged = runtime.run_frame(liquid::FrameInput{11, {}, {light(70)}});
        REQUIRE(unchanged.commands.empty());

        auto fallback = runtime.run_frame(liquid::FrameInput{12, {}, {light(30)}});
        REQUIRE(fallback.commands.size() == 1);
        REQUIRE(runtime.observed_state(
            liquid::AdapterRoute{"test.light"}, liquid::EffectTarget{"office"}) ==
            std::optional<liquid::Value>{liquid::Value{std::uint64_t{30}}});
    }

    {
        Runtime runtime{options(liquid::FeedbackTiming::Deferred)};
        TestAdapter adapter;
        runtime.register_adapter(adapter);

        auto issued = runtime.run_frame(liquid::FrameInput{100, {}, {light(70)}});
        REQUIRE(issued.commands.size() == 1);
        REQUIRE(!runtime.observed_state(
            liquid::AdapterRoute{"test.light"}, liquid::EffectTarget{"office"}));

        auto applied = runtime.run_frame(liquid::FrameInput{101, {}, {light(70)}});
        REQUIRE(applied.commands.empty());
        REQUIRE(applied.reports.size() == 1);
        REQUIRE(runtime.observed_state(
            liquid::AdapterRoute{"test.light"}, liquid::EffectTarget{"office"}) ==
            std::optional<liquid::Value>{liquid::Value{std::uint64_t{70}}});
    }

    {
        Runtime runtime{options(liquid::FeedbackTiming::Immediate)};
        TestAdapter adapter;
        adapter.reportSynchronously = false;
        runtime.register_adapter(adapter);

        auto issued = runtime.run_frame(liquid::FrameInput{0, {}, {light(70)}});
        REQUIRE(issued.commands.size() == 1);
        const auto command = issued.commands.front();
        runtime.run_frame(liquid::FrameInput{249, {}, {light(70)}});
        REQUIRE(adapter.dispatched.size() == 1);
        runtime.run_frame(liquid::FrameInput{250, {}, {light(70)}});
        REQUIRE(adapter.dispatched.size() == 2);
        REQUIRE(adapter.dispatched.back().commandId == command.commandId);

        auto superseding = runtime.run_frame(liquid::FrameInput{251, {}, {light(30)}});
        REQUIRE(superseding.commands.size() == 1);
        REQUIRE(superseding.commands.front().commandId != command.commandId);
        REQUIRE(runtime.command_status(command.commandId) ==
               std::optional<liquid::CommandStatus>{liquid::CommandStatus::Superseded});

        const auto sendResult = adapter.sender.try_send(liquid::EffectReport{
            command.sessionId,
            command.commandId,
            command.effect.adapterRoute,
            command.effect.target,
            liquid::CommandStatus::Applied,
            liquid::Value{std::uint64_t{70}},
            {},
            252
        });
        REQUIRE(sendResult == liquid::FeedbackSendResult::Sent);
        runtime.run_frame(liquid::FrameInput{252, {}, {light(30)}});
        REQUIRE(runtime.command_status(command.commandId) ==
               std::optional<liquid::CommandStatus>{liquid::CommandStatus::Superseded});
        REQUIRE(runtime.observed_state(
            liquid::AdapterRoute{"test.light"}, liquid::EffectTarget{"office"}) ==
            std::optional<liquid::Value>{liquid::Value{std::uint64_t{70}}});
    }

    {
        Runtime runtime{options(liquid::FeedbackTiming::Immediate)};
        TestAdapter adapter;
        adapter.reportSynchronously = false;
        runtime.register_adapter(adapter);
        const auto issued = runtime.run_frame(
            liquid::FrameInput{0, {}, {light(70)}}).commands.front();

        runtime.run_frame(liquid::FrameInput{30000, {}, {light(70)}});
        REQUIRE(runtime.command_status(issued.commandId) ==
               std::optional<liquid::CommandStatus>{liquid::CommandStatus::TimedOut});
    }

    {
        Runtime runtime{options(liquid::FeedbackTiming::Immediate)};
        TestAdapter adapter;
        adapter.reportSynchronously = false;
        adapter.throwOnDispatch = true;
        adapter.adapterCapabilities = {false, false, false};
        runtime.register_adapter(adapter);
        const auto issued = runtime.run_frame(
            liquid::FrameInput{0, {}, {light(70)}}).commands.front();
        REQUIRE(runtime.command_status(issued.commandId) ==
               std::optional<liquid::CommandStatus>{liquid::CommandStatus::Indeterminate});
        REQUIRE(runtime.run_frame(liquid::FrameInput{1, {}, {light(30)}}).commands.empty());
        runtime.reconcile_indeterminate(
            liquid::AdapterRoute{"test.light"},
            liquid::EffectTarget{"office"},
            liquid::Value{std::uint64_t{65}}
        );
        REQUIRE(runtime.command_status(issued.commandId) ==
               std::optional<liquid::CommandStatus>{liquid::CommandStatus::Failed});
    }

    {
        Runtime runtime{options(liquid::FeedbackTiming::Immediate)};
        TestAdapter rejected;
        rejected.disposition = liquid::DispatchDisposition::Rejected;
        runtime.register_adapter(rejected);
        const auto command = runtime.run_frame(
            liquid::FrameInput{0, {}, {light(70)}}).commands.front();
        REQUIRE(runtime.command_status(command.commandId) ==
               std::optional<liquid::CommandStatus>{liquid::CommandStatus::Rejected});
        REQUIRE(!runtime.observed_state(
            liquid::AdapterRoute{"test.light"}, liquid::EffectTarget{"office"}));
    }

    {
        FailingStore store{3};
        auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
        runtimeOptions.eventStore = &store;
        Runtime runtime{runtimeOptions};
        TestAdapter adapter;
        runtime.register_adapter(adapter);
        bool threw = false;
        try {
            runtime.run_frame(liquid::FrameInput{0, {}, {light(70)}});
        } catch (const liquid::EventStoreError&) {
            threw = true;
        }
        REQUIRE(threw);
        REQUIRE(runtime.faulted());
        REQUIRE(runtime.frame() == liquid::FrameNumber{0});
        REQUIRE(adapter.dispatched.empty());
    }

}

TEST_CASE("runtime restores pending commands and retries with the same ID") {
    liquid::EventStoreMetadata metadata;
    metadata.session = liquid::SessionId{42};
    metadata.engineVersion = "0.1.0";
    metadata.feedbackTiming = liquid::FeedbackTiming::Immediate;
    liquid::MemoryEventStore store{metadata};

    liquid::CommandId originalId;
    {
        auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
        runtimeOptions.eventStore = &store;
        Runtime runtime{runtimeOptions};
        TestAdapter adapter;
        adapter.reportSynchronously = false;
        runtime.register_adapter(adapter);
        originalId = runtime.run_frame(
            liquid::FrameInput{0, {}, {light(70)}}).commands.front().commandId;
    }

    auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
    runtimeOptions.eventStore = &store;
    Runtime reopened{runtimeOptions};
    TestAdapter adapter;
    adapter.reportSynchronously = false;
    reopened.register_adapter(adapter);
    const auto frame = reopened.run_frame(
        liquid::FrameInput{250, {}, {light(70)}});

    REQUIRE(frame.commands.empty());
    REQUIRE(adapter.dispatched.size() == 1);
    REQUIRE(adapter.dispatched.front().commandId == originalId);
    REQUIRE(reopened.command_status(originalId) ==
            std::optional<liquid::CommandStatus>{liquid::CommandStatus::Pending});
}

TEST_CASE("runtime restores pending commands after checkpoint retention") {
    liquid::EventStoreMetadata metadata;
    metadata.session = liquid::SessionId{42};
    metadata.engineVersion = "0.1.0";
    metadata.feedbackTiming = liquid::FeedbackTiming::Immediate;
    liquid::MemoryEventStore store{metadata};
    liquid::CommandId originalId;
    {
        auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
        runtimeOptions.eventStore = &store;
        Runtime runtime{runtimeOptions};
        TestAdapter adapter;
        adapter.reportSynchronously = false;
        runtime.register_adapter(adapter);
        originalId = runtime.run_frame(
            liquid::FrameInput{0, {}, {light(70)}}).commands.front().commandId;
        const auto checkpoint = runtime.checkpoint();
        store.retain_from_checkpoint(checkpoint);
    }

    auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
    runtimeOptions.eventStore = &store;
    Runtime reopened{runtimeOptions};
    TestAdapter adapter;
    adapter.reportSynchronously = false;
    reopened.register_adapter(adapter);
    REQUIRE(reopened.run_frame(
        liquid::FrameInput{250, {}, {light(70)}}).commands.empty());
    REQUIRE(adapter.dispatched.size() == 1);
    REQUIRE(adapter.dispatched.front().commandId == originalId);
}

TEST_CASE("runtime restores observed state and suppresses redundant commands") {
    liquid::EventStoreMetadata metadata;
    metadata.session = liquid::SessionId{42};
    metadata.engineVersion = "0.1.0";
    metadata.feedbackTiming = liquid::FeedbackTiming::Immediate;
    liquid::MemoryEventStore store{metadata};

    {
        auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
        runtimeOptions.eventStore = &store;
        Runtime runtime{runtimeOptions};
        TestAdapter adapter;
        runtime.register_adapter(adapter);
        REQUIRE(runtime.run_frame(
            liquid::FrameInput{0, {}, {light(70)}}).commands.size() == 1);
    }

    auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
    runtimeOptions.eventStore = &store;
    Runtime reopened{runtimeOptions};
    TestAdapter adapter;
    reopened.register_adapter(adapter);
    REQUIRE(reopened.run_frame(
        liquid::FrameInput{1, {}, {light(70)}}).commands.empty());
    REQUIRE(adapter.dispatched.empty());
    REQUIRE(reopened.observed_state(
        liquid::AdapterRoute{"test.light"}, liquid::EffectTarget{"office"}) ==
        std::optional<liquid::Value>{liquid::Value{std::uint64_t{70}}});
}

TEST_CASE("runtime blocks uncertain restored commands until reconciliation") {
    liquid::EventStoreMetadata metadata;
    metadata.session = liquid::SessionId{42};
    metadata.engineVersion = "0.1.0";
    metadata.feedbackTiming = liquid::FeedbackTiming::Immediate;
    liquid::MemoryEventStore store{metadata};

    liquid::CommandId originalId;
    {
        auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
        runtimeOptions.eventStore = &store;
        Runtime runtime{runtimeOptions};
        TestAdapter adapter;
        adapter.reportSynchronously = false;
        adapter.adapterCapabilities = {false, false, false};
        runtime.register_adapter(adapter);
        originalId = runtime.run_frame(
            liquid::FrameInput{0, {}, {light(70)}}).commands.front().commandId;
    }

    auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
    runtimeOptions.eventStore = &store;
    Runtime reopened{runtimeOptions};
    TestAdapter adapter;
    adapter.reportSynchronously = false;
    adapter.adapterCapabilities = {false, false, false};
    reopened.register_adapter(adapter);
    REQUIRE(reopened.command_status(originalId) ==
            std::optional<liquid::CommandStatus>{liquid::CommandStatus::Indeterminate});
    REQUIRE(reopened.run_frame(
        liquid::FrameInput{1, {}, {light(30)}}).commands.empty());
    REQUIRE(adapter.dispatched.empty());
}

TEST_CASE("runtime bounds retained terminal command history") {
    auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
    runtimeOptions.maxRetainedCommands = 2;
    Runtime runtime{runtimeOptions};
    TestAdapter adapter;
    runtime.register_adapter(adapter);

    const auto first = runtime.run_frame(
        liquid::FrameInput{0, {}, {light(10)}}).commands.front().commandId;
    const auto second = runtime.run_frame(
        liquid::FrameInput{1, {}, {light(20)}}).commands.front().commandId;
    const auto third = runtime.run_frame(
        liquid::FrameInput{2, {}, {light(30)}}).commands.front().commandId;

    REQUIRE(!runtime.command_status(first));
    REQUIRE(runtime.command_status(second) ==
            std::optional<liquid::CommandStatus>{liquid::CommandStatus::Applied});
    REQUIRE(runtime.command_status(third) ==
            std::optional<liquid::CommandStatus>{liquid::CommandStatus::Applied});
}

TEST_CASE("invalid frame input does not append records or fault the runtime") {
    liquid::EventStoreMetadata metadata;
    metadata.session = liquid::SessionId{42};
    metadata.engineVersion = "0.1.0";
    metadata.feedbackTiming = liquid::FeedbackTiming::Immediate;
    liquid::MemoryEventStore store{metadata};
    auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
    runtimeOptions.eventStore = &store;
    Runtime runtime{runtimeOptions};
    TestAdapter adapter;
    runtime.register_adapter(adapter);
    runtime.run_frame(liquid::FrameInput{10, {}, {light(70)}});
    const auto recordCount = store.read_all().size();

    bool threw = false;
    try {
        runtime.run_frame(liquid::FrameInput{9, {}, {light(30)}});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    REQUIRE(threw);
    REQUIRE(store.read_all().size() == recordCount);
    REQUIRE(runtime.frame() == liquid::FrameNumber{1});
    REQUIRE(!runtime.faulted());
}

TEST_CASE("duplicate effect targets are rejected before frame mutation") {
    liquid::EventStoreMetadata metadata;
    metadata.session = liquid::SessionId{42};
    metadata.engineVersion = "0.1.0";
    metadata.feedbackTiming = liquid::FeedbackTiming::Immediate;
    liquid::MemoryEventStore store{metadata};
    auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
    runtimeOptions.eventStore = &store;
    Runtime runtime{runtimeOptions};
    TestAdapter adapter;
    runtime.register_adapter(adapter);
    const auto recordCount = store.read_all().size();

    bool threw = false;
    try {
        runtime.run_frame(liquid::FrameInput{0, {}, {light(70), light(30)}});
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    REQUIRE(threw);
    REQUIRE(store.read_all().size() == recordCount);
    REQUIRE(runtime.frame() == liquid::FrameNumber{0});
    REQUIRE(adapter.dispatched.empty());
}

TEST_CASE("runtime journals duplicate and rejected report classifications") {
    liquid::EventStoreMetadata metadata;
    metadata.session = liquid::SessionId{42};
    metadata.engineVersion = "0.1.0";
    metadata.feedbackTiming = liquid::FeedbackTiming::Deferred;
    liquid::MemoryEventStore store{metadata};
    auto runtimeOptions = options(liquid::FeedbackTiming::Deferred);
    runtimeOptions.eventStore = &store;
    Runtime runtime{runtimeOptions};
    TestAdapter adapter;
    adapter.reportSynchronously = false;
    runtime.register_adapter(adapter);
    const auto command = runtime.run_frame(
        liquid::FrameInput{0, {}, {light(70)}}).commands.front();

    const liquid::EffectReport applied{
        command.sessionId, command.commandId, command.effect.adapterRoute,
        command.effect.target, liquid::CommandStatus::Applied,
        command.effect.desiredValue, {}, 1};
    REQUIRE(adapter.sender.try_send(applied) == liquid::FeedbackSendResult::Sent);
    REQUIRE(adapter.sender.try_send(liquid::EffectReport{
        command.sessionId, command.commandId, command.effect.adapterRoute,
        liquid::EffectTarget{"wrong"}, liquid::CommandStatus::Applied,
        command.effect.desiredValue, {}, 1}) == liquid::FeedbackSendResult::Sent);
    REQUIRE(runtime.run_frame(
        liquid::FrameInput{1, {}, {light(70)}}).reports.size() == 1);

    REQUIRE(adapter.sender.try_send(applied) == liquid::FeedbackSendResult::Sent);
    auto conflicting = applied;
    conflicting.observedValue = liquid::Value{std::uint64_t{60}};
    REQUIRE(adapter.sender.try_send(conflicting) == liquid::FeedbackSendResult::Sent);
    auto unknown = applied;
    unknown.commandId = liquid::CommandId{999};
    REQUIRE(adapter.sender.try_send(unknown) == liquid::FeedbackSendResult::Sent);
    REQUIRE(runtime.run_frame(
        liquid::FrameInput{2, {}, {light(70)}}).reports.empty());

    std::vector<std::string> dispositions;
    for (const auto& record : store.read_all()) {
        if (record.type != liquid::EventType::ReportReceived)
            continue;
        dispositions.push_back(
            record.payload.as_object().at("disposition").as_string());
    }
    REQUIRE(dispositions == std::vector<std::string>{
        "accepted",
        "rejected-mismatched-target",
        "duplicate",
        "rejected-conflicting-duplicate",
        "rejected-unknown-or-cross-session"
    });
}

TEST_CASE("failed frames preserve partial mutation and system evidence") {
    liquid::EventStoreMetadata metadata;
    metadata.session = liquid::SessionId{42};
    metadata.engineVersion = "0.1.0";
    metadata.feedbackTiming = liquid::FeedbackTiming::Immediate;
    liquid::MemoryEventStore store{metadata};
    auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
    runtimeOptions.eventStore = &store;
    Runtime runtime{runtimeOptions};
    auto& world = runtime.world();
    const auto type = world.register_component<std::uint64_t>(
        "tests.Level", 1, unsigned_codec());
    world.add_component(type, "level", std::uint64_t{10});
    const auto behavior = world.create_behavior();
    world.grant_component_access(
        type, behavior, "level", liquid::ComponentAccessMode::ReadWrite);
    liquid::Signature signature;
    signature.set(type.id);
    world.register_system<MutatingThrowingSystem>(signature);
    auto& system = world.get_system<MutatingThrowingSystem>();
    system.type = type;
    system.behavior = behavior;

    bool threw = false;
    try {
        runtime.run_frame(liquid::FrameInput{0, {}, {}});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    REQUIRE(threw);
    REQUIRE(*world.get_component_named(type, "level") == 80);

    bool sawMutation = false;
    bool sawFailure = false;
    for (const auto& record : store.read_all()) {
        const auto& object = record.payload.as_object();
        if (record.type == liquid::EventType::ComponentMutated) {
            sawMutation = sawMutation ||
                (object.at("before").kind() == liquid::Value::Kind::UnsignedInteger &&
                 object.at("after").kind() == liquid::Value::Kind::UnsignedInteger &&
                 object.at("before").as_unsigned_integer() == 10 &&
                 object.at("after").as_unsigned_integer() == 80);
        } else if (record.type == liquid::EventType::FrameFailed) {
            sawFailure = object.at("frame").as_unsigned_integer() == 0 &&
                object.at("phase").as_string() == "run_decision_systems" &&
                object.at("system").as_string() ==
                    "tests.runtime.effects.MutatingThrowingSystem@1" &&
                object.at("message").as_string() == "mutation then failure";
        }
    }
    REQUIRE(sawMutation);
    REQUIRE(sawFailure);
}

TEST_CASE("command and record sequences never wrap") {
    liquid::EventStoreMetadata metadata;
    metadata.session = liquid::SessionId{42};
    metadata.engineVersion = "0.1.0";
    metadata.feedbackTiming = liquid::FeedbackTiming::Immediate;
    liquid::MemoryEventStore sequenceStore{metadata};
    sequenceStore.set_next_sequence_for_test(liquid::RecordId{
        std::numeric_limits<std::uint64_t>::max()});
    sequenceStore.append(liquid::EventData{
        liquid::EventType::ConfigurationChanged,
        1,
        liquid::Value{liquid::Value::Object{}}});
    bool recordExhausted = false;
    try {
        sequenceStore.append(liquid::EventData{
            liquid::EventType::ConfigurationChanged,
            1,
            liquid::Value{liquid::Value::Object{}}});
    } catch (const liquid::EventStoreError&) {
        recordExhausted = true;
    }
    REQUIRE(recordExhausted);

    liquid::MemoryEventStore commandStore{metadata};
    liquid::Value::Object command;
    command.emplace("key", liquid::Value{"command:max"});
    command.emplace("value", liquid::Value{"pending"});
    command.emplace("command_id", liquid::Value{
        std::numeric_limits<std::uint64_t>::max()});
    command.emplace("route", liquid::Value{"test.light"});
    command.emplace("target", liquid::Value{"office"});
    command.emplace("desired", liquid::Value{std::uint64_t{70}});
    command.emplace("issued_at", liquid::Value{std::uint64_t{0}});
    commandStore.append(liquid::EventData{
        liquid::EventType::CommandIssued, 1, liquid::Value{std::move(command)}});
    auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
    runtimeOptions.eventStore = &commandStore;
    bool commandExhausted = false;
    try {
        Runtime runtime{runtimeOptions};
    } catch (const liquid::EventStoreError&) {
        commandExhausted = true;
    }
    REQUIRE(commandExhausted);
}

TEST_CASE("runtime owns registered adapters for its full lifetime") {
    std::weak_ptr<TestAdapter> lifetime;
    {
        Runtime runtime{options(liquid::FeedbackTiming::Immediate)};
        auto adapter = std::make_shared<TestAdapter>();
        lifetime = adapter;
        runtime.register_adapter(adapter);
        adapter.reset();
        REQUIRE(!lifetime.expired());
        REQUIRE(runtime.run_frame(
            liquid::FrameInput{0, {}, {light(70)}}).commands.size() == 1);
    }
    REQUIRE(lifetime.expired());
}

TEST_CASE("runtime command outcomes never imply unreported observed state") {
    const auto verify_terminal_outcome = [](liquid::CommandStatus status) {
        Runtime runtime{options(liquid::FeedbackTiming::Immediate)};
        TestAdapter adapter;
        adapter.reportSynchronously = false;
        runtime.register_adapter(adapter);
        const auto command = runtime.run_frame(
            liquid::FrameInput{0, {}, {light(70)}}).commands.front();

        REQUIRE(adapter.sender.try_send(outcome_report(command, status, std::nullopt, 1)) ==
                liquid::FeedbackSendResult::Sent);
        const auto feedback = runtime.run_frame(liquid::FrameInput{1, {}, {}});

        REQUIRE(feedback.reports.size() == 1);
        REQUIRE(runtime.command_status(command.commandId) ==
                std::optional<liquid::CommandStatus>{status});
        REQUIRE(!runtime.observed_state(
            liquid::AdapterRoute{"test.light"}, liquid::EffectTarget{"office"}));
    };

    verify_terminal_outcome(liquid::CommandStatus::Rejected);
    verify_terminal_outcome(liquid::CommandStatus::Failed);
}

TEST_CASE("runtime ignores unknown mismatched and malformed reports") {
    Runtime runtime{options(liquid::FeedbackTiming::Immediate)};
    TestAdapter adapter;
    adapter.reportSynchronously = false;
    runtime.register_adapter(adapter);
    const auto command = runtime.run_frame(
        liquid::FrameInput{0, {}, {light(70)}}).commands.front();

    auto crossSession = outcome_report(
        command,
        liquid::CommandStatus::Applied,
        liquid::Value{std::uint64_t{70}},
        1
    );
    crossSession.sessionId = liquid::SessionId{99};
    REQUIRE(adapter.sender.try_send(crossSession) == liquid::FeedbackSendResult::Sent);

    auto unknown = crossSession;
    unknown.sessionId = command.sessionId;
    unknown.commandId = liquid::CommandId{999};
    REQUIRE(adapter.sender.try_send(unknown) == liquid::FeedbackSendResult::Sent);

    auto wrongRoute = outcome_report(
        command,
        liquid::CommandStatus::Applied,
        liquid::Value{std::uint64_t{70}},
        1
    );
    wrongRoute.adapterRoute = liquid::AdapterRoute{"test.other"};
    REQUIRE(adapter.sender.try_send(wrongRoute) == liquid::FeedbackSendResult::Sent);

    auto wrongTarget = outcome_report(
        command,
        liquid::CommandStatus::Applied,
        liquid::Value{std::uint64_t{70}},
        1
    );
    wrongTarget.target = liquid::EffectTarget{"elsewhere"};
    REQUIRE(adapter.sender.try_send(wrongTarget) == liquid::FeedbackSendResult::Sent);

    REQUIRE_THROWS_AS(
        adapter.sender.try_send(outcome_report(
            command, liquid::CommandStatus::Applied, std::nullopt, 1)),
        std::invalid_argument
    );
    REQUIRE_THROWS_AS(
        adapter.sender.try_send(outcome_report(
            command,
            liquid::CommandStatus::Rejected,
            liquid::Value{std::uint64_t{70}},
            1)),
        std::invalid_argument
    );
    REQUIRE_THROWS_AS(
        adapter.sender.try_send(outcome_report(
            command, liquid::CommandStatus::Pending, std::nullopt, 1)),
        std::invalid_argument
    );

    const auto feedback = runtime.run_frame(liquid::FrameInput{1, {}, {}});
    REQUIRE(feedback.reports.empty());
    REQUIRE(runtime.command_status(command.commandId) ==
            std::optional<liquid::CommandStatus>{liquid::CommandStatus::Pending});
    REQUIRE(!runtime.observed_state(
        liquid::AdapterRoute{"test.light"}, liquid::EffectTarget{"office"}));
}

TEST_CASE("runtime records one identical report and rejects a conflicting duplicate") {
    Runtime runtime{options(liquid::FeedbackTiming::Immediate)};
    TestAdapter adapter;
    adapter.reportSynchronously = false;
    runtime.register_adapter(adapter);
    const auto command = runtime.run_frame(
        liquid::FrameInput{0, {}, {light(70)}}).commands.front();
    const auto applied = outcome_report(
        command,
        liquid::CommandStatus::Applied,
        liquid::Value{std::uint64_t{70}},
        1
    );

    REQUIRE(adapter.sender.try_send(applied) == liquid::FeedbackSendResult::Sent);
    REQUIRE(adapter.sender.try_send(applied) == liquid::FeedbackSendResult::Sent);
    const auto identical = runtime.run_frame(liquid::FrameInput{1, {}, {}});
    REQUIRE(identical.reports.size() == 1);

    auto conflicting = applied;
    conflicting.observedValue = liquid::Value{std::uint64_t{30}};
    conflicting.reportedAtMs = 2;
    REQUIRE(adapter.sender.try_send(conflicting) == liquid::FeedbackSendResult::Sent);
    const auto conflict = runtime.run_frame(liquid::FrameInput{2, {}, {}});
    REQUIRE(conflict.reports.empty());
    REQUIRE(runtime.command_status(command.commandId) ==
            std::optional<liquid::CommandStatus>{liquid::CommandStatus::Applied});
    REQUIRE(runtime.observed_state(
        liquid::AdapterRoute{"test.light"}, liquid::EffectTarget{"office"}) ==
        std::optional<liquid::Value>{liquid::Value{std::uint64_t{70}}});
}

TEST_CASE("newer authoritative feedback wins stale out of order reports") {
    Runtime runtime{options(liquid::FeedbackTiming::Immediate)};
    TestAdapter adapter;
    adapter.reportSynchronously = false;
    runtime.register_adapter(adapter);
    const auto older = runtime.run_frame(
        liquid::FrameInput{0, {}, {light(70)}}).commands.front();
    const auto newer = runtime.run_frame(
        liquid::FrameInput{1, {}, {light(30)}}).commands.front();

    REQUIRE(runtime.command_status(older.commandId) ==
            std::optional<liquid::CommandStatus>{liquid::CommandStatus::Superseded});
    REQUIRE(adapter.sender.try_send(outcome_report(
        newer,
        liquid::CommandStatus::Applied,
        liquid::Value{std::uint64_t{30}},
        2)) == liquid::FeedbackSendResult::Sent);
    REQUIRE(adapter.sender.try_send(outcome_report(
        older,
        liquid::CommandStatus::Applied,
        liquid::Value{std::uint64_t{70}},
        3)) == liquid::FeedbackSendResult::Sent);

    const auto feedback = runtime.run_frame(liquid::FrameInput{3, {}, {}});
    REQUIRE(feedback.reports.size() == 2);
    REQUIRE(runtime.command_status(older.commandId) ==
            std::optional<liquid::CommandStatus>{liquid::CommandStatus::Superseded});
    REQUIRE(runtime.command_status(newer.commandId) ==
            std::optional<liquid::CommandStatus>{liquid::CommandStatus::Applied});
    REQUIRE(runtime.observed_state(
        liquid::AdapterRoute{"test.light"}, liquid::EffectTarget{"office"}) ==
        std::optional<liquid::Value>{liquid::Value{std::uint64_t{30}}});
}

TEST_CASE("late applied feedback remains provisional until a newer command reports") {
    Runtime runtime{options(liquid::FeedbackTiming::Immediate)};
    TestAdapter adapter;
    adapter.reportSynchronously = false;
    runtime.register_adapter(adapter);
    const auto older = runtime.run_frame(
        liquid::FrameInput{0, {}, {light(70)}}).commands.front();
    const auto newer = runtime.run_frame(
        liquid::FrameInput{1, {}, {light(30)}}).commands.front();

    REQUIRE(adapter.sender.try_send(outcome_report(
        older,
        liquid::CommandStatus::Applied,
        liquid::Value{std::uint64_t{70}},
        2)) == liquid::FeedbackSendResult::Sent);
    runtime.run_frame(liquid::FrameInput{2, {}, {}});
    REQUIRE(runtime.observed_state(
        liquid::AdapterRoute{"test.light"}, liquid::EffectTarget{"office"}) ==
        std::optional<liquid::Value>{liquid::Value{std::uint64_t{70}}});

    REQUIRE(adapter.sender.try_send(outcome_report(
        newer,
        liquid::CommandStatus::Applied,
        liquid::Value{std::uint64_t{30}},
        3)) == liquid::FeedbackSendResult::Sent);
    runtime.run_frame(liquid::FrameInput{3, {}, {}});
    REQUIRE(runtime.observed_state(
        liquid::AdapterRoute{"test.light"}, liquid::EffectTarget{"office"}) ==
        std::optional<liquid::Value>{liquid::Value{std::uint64_t{30}}});
}

TEST_CASE("superseded non-applied feedback is stale and cannot mutate state") {
    liquid::EventStoreMetadata metadata;
    metadata.session = liquid::SessionId{42};
    metadata.engineVersion = "0.1.0";
    metadata.feedbackTiming = liquid::FeedbackTiming::Immediate;
    liquid::MemoryEventStore store{metadata};
    auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
    runtimeOptions.eventStore = &store;
    Runtime runtime{runtimeOptions};
    TestAdapter adapter;
    adapter.reportSynchronously = false;
    runtime.register_adapter(adapter);
    const auto older = runtime.run_frame(
        liquid::FrameInput{0, {}, {light(70)}}).commands.front();
    const auto newer = runtime.run_frame(
        liquid::FrameInput{1, {}, {light(30)}}).commands.front();

    REQUIRE(adapter.sender.try_send(outcome_report(
        older, liquid::CommandStatus::Failed, std::nullopt, 2)) ==
        liquid::FeedbackSendResult::Sent);
    REQUIRE(runtime.run_frame(liquid::FrameInput{2, {}, {}}).reports.empty());
    REQUIRE(runtime.command_status(older.commandId) ==
            std::optional<liquid::CommandStatus>{liquid::CommandStatus::Superseded});
    REQUIRE(runtime.command_status(newer.commandId) ==
            std::optional<liquid::CommandStatus>{liquid::CommandStatus::Pending});
    REQUIRE(!runtime.observed_state(
        liquid::AdapterRoute{"test.light"}, liquid::EffectTarget{"office"}));

    std::vector<std::string> dispositions;
    for (const auto& record : store.read_all()) {
        if (record.type == liquid::EventType::ReportReceived) {
            dispositions.push_back(
                record.payload.as_object().at("disposition").as_string());
        }
    }
    REQUIRE(dispositions == std::vector<std::string>{"rejected-stale-terminal"});
}

TEST_CASE("silent commands retry on the default schedule with one stable ID") {
    Runtime runtime{options(liquid::FeedbackTiming::Immediate)};
    TestAdapter adapter;
    adapter.reportSynchronously = false;
    runtime.register_adapter(adapter);
    const auto command = runtime.run_frame(
        liquid::FrameInput{0, {}, {light(70)}}).commands.front();

    runtime.run_frame(liquid::FrameInput{249, {}, {}});
    REQUIRE(adapter.dispatched.size() == 1);
    for (const auto now : {250ULL, 500ULL, 1000ULL, 2000ULL, 4000ULL})
        runtime.run_frame(liquid::FrameInput{now, {}, {}});

    REQUIRE(adapter.dispatched.size() == 6);
    for (const auto& attempt : adapter.dispatched)
        REQUIRE(attempt.commandId == command.commandId);
    runtime.run_frame(liquid::FrameInput{29999, {}, {}});
    REQUIRE(runtime.command_status(command.commandId) ==
            std::optional<liquid::CommandStatus>{liquid::CommandStatus::Pending});
    runtime.run_frame(liquid::FrameInput{30000, {}, {}});
    REQUIRE(runtime.command_status(command.commandId) ==
            std::optional<liquid::CommandStatus>{liquid::CommandStatus::TimedOut});
    REQUIRE(!runtime.observed_state(
        liquid::AdapterRoute{"test.light"}, liquid::EffectTarget{"office"}));
}

TEST_CASE("explicit session and inputs produce identical runtime records") {
    auto record_run = [] {
        liquid::EventStoreMetadata metadata;
        metadata.session = liquid::SessionId{4242};
        metadata.engineVersion = "0.1.0";
        metadata.feedbackTiming = liquid::FeedbackTiming::Immediate;
        auto store = std::make_unique<liquid::MemoryEventStore>(metadata);
        auto runtimeOptions = options(liquid::FeedbackTiming::Immediate);
        runtimeOptions.sessionId = metadata.session;
        runtimeOptions.eventStore = store.get();
        Runtime runtime{runtimeOptions};
        auto& world = runtime.world();
        const auto type = world.register_component<std::uint64_t>(
            "tests.Level", 1, unsigned_codec());
        world.add_component(type, "level", std::uint64_t{10});
        const auto behavior = world.create_behavior();
        world.grant_component_access(
            type, behavior, "level", liquid::ComponentAccessMode::ReadWrite);
        const auto slots = world.get_components(type, behavior);
        world.create_intent(
            behavior, type, slots.at("level"),
            liquid::IntentLifetime::persistent(), std::uint64_t{70});
        runtime.run_frame(liquid::FrameInput{
            100, {{type.id, slots}}, {}});
        return store->read_all();
    };

    REQUIRE(record_run() == record_run());
}

TEST_CASE("runtime records destroyed-intent evidence when an intent expires") {
    liquid::EventStoreMetadata metadata;
    metadata.session = liquid::SessionId{42};
    metadata.engineVersion = "0.1.0";
    metadata.feedbackTiming = liquid::FeedbackTiming::Deferred;
    liquid::MemoryEventStore store{metadata};
    auto runtimeOptions = options(FeedbackTiming::Deferred);
    runtimeOptions.eventStore = &store;
    Runtime runtime{runtimeOptions};

    World& world = runtime.world();
    const auto levelType = world.register_component<std::uint64_t>(
        "tests.Level", 1, unsigned_codec());
    world.add_component(levelType, "level", std::uint64_t{10});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(
        levelType, behavior, "level", ComponentAccessMode::ReadWrite);
    const ComponentSlotId slot =
        world.get_components(levelType, behavior).at("level");
    const IntentId expiring = world.create_intent(
        behavior, levelType, slot,
        IntentLifetime::until_time(105), std::uint64_t{70},
        IntentPriority::High);

    runtime.run_frame(FrameInput{100, {}, {}});
    runtime.run_frame(FrameInput{105, {}, {}});

    std::size_t created = 0;
    std::size_t destroyed = 0;
    for (const liquid::EventRecord& record : store.read_all()) {
        if (record.type == liquid::EventType::IntentCreated) {
            created++;
            REQUIRE(record.payload.as_object().at("intent_slot")
                .as_unsigned_integer() == expiring.slot);
        }
        if (record.type == liquid::EventType::IntentDestroyed) {
            destroyed++;
            REQUIRE(record.payload.as_object().at("intent_slot")
                .as_unsigned_integer() == expiring.slot);
        }
    }
    REQUIRE(created == 1);
    REQUIRE(destroyed == 1);
}

TEST_CASE("runtime rejects an event store with mismatched session metadata") {
    liquid::EventStoreMetadata metadata;
    metadata.session = liquid::SessionId{7};
    metadata.engineVersion = "0.1.0";
    metadata.feedbackTiming = liquid::FeedbackTiming::Deferred;
    liquid::MemoryEventStore store{metadata};

    auto mismatchedSession = options(FeedbackTiming::Deferred);
    mismatchedSession.eventStore = &store;
    REQUIRE_THROWS_AS(Runtime{mismatchedSession}, std::invalid_argument);

    auto mismatchedTiming = options(FeedbackTiming::Immediate);
    mismatchedTiming.sessionId = liquid::SessionId{7};
    mismatchedTiming.eventStore = &store;
    REQUIRE_THROWS_AS(Runtime{mismatchedTiming}, std::invalid_argument);
}

TEST_CASE("effect routes and targets reject invalid text") {
    REQUIRE_THROWS_AS(
        liquid::AdapterRoute{"bad route!"}, std::invalid_argument);
    REQUIRE_THROWS_AS(
        liquid::EffectTarget{std::string(2048, 'x')}, std::invalid_argument);
}

TEST_CASE("effect reports keep an explicitly valid state revision") {
    const liquid::EffectReport explicitRevision{
        liquid::SessionId{42},
        liquid::CommandId{3},
        liquid::AdapterRoute{"test.light"},
        liquid::EffectTarget{"level"},
        liquid::CommandStatus::Applied,
        liquid::Value{std::uint64_t{70}},
        {},
        100,
        liquid::StateRevision{9}};
    REQUIRE(explicitRevision.stateRevision == liquid::StateRevision{9});

    const liquid::EffectReport derivedRevision{
        liquid::SessionId{42},
        liquid::CommandId{3},
        liquid::AdapterRoute{"test.light"},
        liquid::EffectTarget{"level"},
        liquid::CommandStatus::Applied,
        liquid::Value{std::uint64_t{70}},
        {},
        100};
    REQUIRE(derivedRevision.stateRevision == liquid::StateRevision{3});
}

TEST_CASE("projecting an observation equal to confirmed state is a no-op") {
    Runtime runtime{options(FeedbackTiming::Deferred)};
    auto adapter = std::make_shared<TestAdapter>();
    runtime.register_adapter(adapter);

    World& world = runtime.world();
    const auto levelType = world.register_component<std::uint64_t>(
        "tests.Level", 1, unsigned_codec());
    world.register_effect_codec(levelType, light_effect_codec());
    world.add_component(levelType, "level", std::uint64_t{10});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(
        levelType, behavior, "level", ComponentAccessMode::ReadWrite);
    runtime.bind_effect_component(levelType, "level", EffectTarget{"level"});

    liquid::FeedbackSender sender = runtime.feedback_sender();
    REQUIRE(sender.try_send(liquid::ExternalObservation{
        liquid::SessionId{42},
        liquid::AdapterRoute{"test.light"},
        liquid::EffectTarget{"level"},
        liquid::Value{std::uint64_t{10}},
        liquid::StateRevision{1},
        100}) == liquid::FeedbackSendResult::Sent);

    const FrameResult frame = runtime.run_frame(FrameInput{100, {}, {}});
    REQUIRE(frame.observations.size() == 1);
    REQUIRE(*world.read_component(levelType, behavior, "level") == 10);
}

TEST_CASE("effect records compare by value") {
    const liquid::EffectReport applied{
        liquid::SessionId{42},
        liquid::CommandId{3},
        liquid::AdapterRoute{"test.light"},
        liquid::EffectTarget{"level"},
        liquid::CommandStatus::Applied,
        liquid::Value{std::uint64_t{70}},
        {},
        100,
        liquid::StateRevision{9}};
    liquid::EffectReport laterRevision = applied;
    laterRevision.stateRevision = liquid::StateRevision{10};
    REQUIRE(applied == applied);
    REQUIRE_FALSE(applied == laterRevision);

    const liquid::ResolvedEffect desire{
        liquid::AdapterRoute{"test.light"},
        liquid::EffectTarget{"level"},
        liquid::Value{std::uint64_t{70}}};
    const liquid::ResolvedEffect otherDesire{
        liquid::AdapterRoute{"test.light"},
        liquid::EffectTarget{"level"},
        liquid::Value{std::uint64_t{30}}};
    REQUIRE(desire == desire);
    REQUIRE_FALSE(desire == otherDesire);

    const liquid::EffectCommand command{
        liquid::SessionId{42}, liquid::CommandId{3}, desire, 100};
    liquid::EffectCommand reissued = command;
    reissued.issuedAtMs = 105;
    REQUIRE(command == command);
    REQUIRE_FALSE(command == reissued);

    const liquid::ExternalObservation observation{
        liquid::SessionId{42},
        liquid::AdapterRoute{"test.light"},
        liquid::EffectTarget{"level"},
        liquid::Value{std::uint64_t{15}},
        liquid::StateRevision{2},
        115};
    liquid::ExternalObservation newer = observation;
    newer.observedAtMs = 120;
    REQUIRE(observation == observation);
    REQUIRE_FALSE(observation == newer);

    const liquid::EventRecord record{
        liquid::RecordId{1}, liquid::EventType::FrameStarted, 1,
        liquid::Value{std::uint64_t{100}}};
    liquid::EventRecord differentPayload = record;
    differentPayload.payload = liquid::Value{std::uint64_t{105}};
    REQUIRE(record == record);
    REQUIRE_FALSE(record == differentPayload);
}
