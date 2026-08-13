#include "liquid/Runtime.hpp"
#include "liquid/events/FileEventStore.hpp"
#include "liquid/events/Replay.hpp"
#include "liquid/simulation/InMemoryAdapter.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace liquid;

namespace {

class TemporarySimulationFile {
    std::filesystem::path filePath;

public:
    TemporarySimulationFile() {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch().count();
        filePath = std::filesystem::temp_directory_path() /
            ("liquid-simulation-replay-" + std::to_string(nonce));
        std::error_code ignored;
        std::filesystem::remove(filePath, ignored);
        std::filesystem::remove(filePath.string() + ".replacement", ignored);
        std::filesystem::remove(filePath.string() + ".lock", ignored);
    }

    ~TemporarySimulationFile() {
        std::error_code ignored;
        std::filesystem::remove(filePath, ignored);
        std::filesystem::remove(filePath.string() + ".replacement", ignored);
        std::filesystem::remove(filePath.string() + ".lock", ignored);
    }

    const std::filesystem::path& path() const {
        return filePath;
    }
};

liquid::ResolvedEffect simulated_effect(
    const std::string& route,
    const std::string& target,
    std::uint64_t desired
) {
    return liquid::ResolvedEffect{
        liquid::AdapterRoute{route},
        liquid::EffectTarget{target},
        liquid::Value{desired}
    };
}

liquid::RuntimeOptions simulation_options(
    std::uint64_t session,
    liquid::FeedbackTiming timing = liquid::FeedbackTiming::Immediate
) {
    liquid::RuntimeOptions result;
    result.sessionId = liquid::SessionId{session};
    result.feedbackTiming = timing;
    result.allowVolatileEffects = true;
    return result;
}

}

TEST_CASE("test_simulation_adapter") {
    liquid::RuntimeOptions options;
    options.sessionId = liquid::SessionId{9};
    options.feedbackTiming = liquid::FeedbackTiming::Deferred;
    options.allowVolatileEffects = true;
    Runtime runtime{options};

    liquid::simulation::AdapterBehavior behavior;
    behavior.latencyMs = 5;
    behavior.duplicateReports = 1;
    behavior.normalize = [](const liquid::Value& value) {
        return liquid::Value{value.as_unsigned_integer() > 100
            ? std::uint64_t{100}
            : value.as_unsigned_integer()};
    };
    liquid::simulation::InMemoryAdapter adapter{
        liquid::AdapterRoute{"sim.light"}, behavior};
    runtime.register_adapter(adapter);

    liquid::ResolvedEffect effect{
        liquid::AdapterRoute{"sim.light"},
        liquid::EffectTarget{"office"},
        liquid::Value{std::uint64_t{120}}
    };
    const auto issued = runtime.run_frame(liquid::FrameInput{100, {}, {effect}});
    REQUIRE(issued.commands.size() == 1);
    REQUIRE(adapter.pending() == 2);
    REQUIRE(adapter.deliver_through(104) == 0);
    REQUIRE(adapter.deliver_through(105) == 2);
    const auto applied = runtime.run_frame(liquid::FrameInput{105, {}, {effect}});
    REQUIRE(applied.reports.size() == 1);
    REQUIRE(runtime.observed_state(
        liquid::AdapterRoute{"sim.light"}, liquid::EffectTarget{"office"}) ==
        liquid::Value{std::uint64_t{100}});

    auto feedback = liquid::make_feedback_channel(1);
    const auto makeCommand = [](std::uint64_t id, std::uint64_t issuedAt = 10) {
        return liquid::EffectCommand{
            liquid::SessionId{9},
            liquid::CommandId{id},
            liquid::ResolvedEffect{
                liquid::AdapterRoute{"bounded.sim"},
                liquid::EffectTarget{"office"},
                liquid::Value{std::uint64_t{70}}
            },
            issuedAt
        };
    };

    liquid::simulation::AdapterBehavior delayedBehavior;
    delayedBehavior.latencyMs = 5;
    liquid::simulation::AdapterLimits idempotentLimits;
    idempotentLimits.maxPendingReports = 4;
    idempotentLimits.maxCachedOutcomes = 1;
    liquid::simulation::InMemoryAdapter idempotent{
        liquid::AdapterRoute{"bounded.sim"},
        delayedBehavior,
        liquid::AdapterCapabilities{true, true, true},
        idempotentLimits
    };
    const auto firstDispatch = idempotent.dispatch(makeCommand(10), feedback.sender);
    const auto retryDispatch = idempotent.dispatch(makeCommand(10), feedback.sender);
    REQUIRE(firstDispatch == retryDispatch);
    REQUIRE(idempotent.pending() == 1);

    bool conflictingReuse = false;
    try {
        auto conflicting = makeCommand(10);
        conflicting.effect.desiredValue = liquid::Value{std::uint64_t{30}};
        idempotent.dispatch(conflicting, feedback.sender);
    } catch (const std::invalid_argument&) {
        conflictingReuse = true;
    }
    REQUIRE(conflictingReuse);

    const auto cacheFull = idempotent.dispatch(makeCommand(11), feedback.sender);
    REQUIRE(cacheFull.disposition == liquid::DispatchDisposition::Failed);
    REQUIRE(idempotent.pending() == 1);

    liquid::simulation::AdapterLimits pendingLimits;
    pendingLimits.maxDuplicateReports = 2;
    pendingLimits.maxPendingReports = 1;
    delayedBehavior.duplicateReports = 1;
    liquid::simulation::InMemoryAdapter boundedPending{
        liquid::AdapterRoute{"bounded.sim"},
        delayedBehavior,
        liquid::AdapterCapabilities{false, true, true},
        pendingLimits
    };
    const auto pendingFull = boundedPending.dispatch(makeCommand(20), feedback.sender);
    REQUIRE(pendingFull.disposition == liquid::DispatchDisposition::Failed);
    REQUIRE(boundedPending.pending() == 0);
    REQUIRE(!boundedPending.state(liquid::EffectTarget{"office"}));

    bool duplicateLimit = false;
    try {
        delayedBehavior.duplicateReports = 3;
        boundedPending.set_behavior(delayedBehavior);
    } catch (const std::invalid_argument&) {
        duplicateLimit = true;
    }
    REQUIRE(duplicateLimit);

    liquid::simulation::AdapterBehavior overflowBehavior;
    overflowBehavior.latencyMs = 1;
    liquid::simulation::InMemoryAdapter overflowAdapter{
        liquid::AdapterRoute{"bounded.sim"}, overflowBehavior};
    REQUIRE(!overflowAdapter.capabilities().nativeIdempotency);
    const auto overflow = overflowAdapter.dispatch(
        makeCommand(30, std::numeric_limits<std::uint64_t>::max()),
        feedback.sender
    );
    REQUIRE(overflow.disposition == liquid::DispatchDisposition::Failed);
    REQUIRE(overflowAdapter.pending() == 0);
    REQUIRE(!overflowAdapter.state(liquid::EffectTarget{"office"}));

    liquid::simulation::AdapterBehavior queueBehavior;
    queueBehavior.latencyMs = 1;
    liquid::simulation::AdapterLimits queueLimits;
    queueLimits.maxPendingReports = 2;
    liquid::simulation::InMemoryAdapter queueAdapter{
        liquid::AdapterRoute{"bounded.sim"},
        queueBehavior,
        liquid::AdapterCapabilities{false, true, true},
        queueLimits
    };
    queueAdapter.dispatch(makeCommand(40, 0), feedback.sender);
    queueAdapter.dispatch(makeCommand(41, 0), feedback.sender);
    REQUIRE(queueAdapter.deliver_through(1) == 1);
    REQUIRE(queueAdapter.pending() == 1);
    REQUIRE(feedback.receiver.drain().size() == 1);
    REQUIRE(queueAdapter.deliver_through(1) == 1);
    REQUIRE(queueAdapter.pending() == 0);

    behavior.latencyMs = 0;
    behavior.duplicateReports = 0;
    behavior.normalize = {};
    behavior.outcome = liquid::CommandStatus::Rejected;
    adapter.set_behavior(behavior);
    effect.desiredValue = liquid::Value{std::uint64_t{30}};
    const auto rejected = runtime.run_frame(liquid::FrameInput{106, {}, {effect}});
    REQUIRE(rejected.commands.size() == 1);
    const auto rejectionApplied = runtime.run_frame(
        liquid::FrameInput{107, {}, {effect}});
    REQUIRE(rejectionApplied.reports.size() == 1);
    REQUIRE(runtime.observed_state(
        liquid::AdapterRoute{"sim.light"}, liquid::EffectTarget{"office"}) ==
        liquid::Value{std::uint64_t{100}});
}

TEST_CASE("simulator exposes truthful failure and delivery modes") {
    {
        auto options = simulation_options(20);
        Runtime runtime{options};
        liquid::simulation::AdapterBehavior behavior;
        behavior.outcome = liquid::CommandStatus::Failed;
        liquid::simulation::InMemoryAdapter adapter{
            liquid::AdapterRoute{"sim.failure"}, behavior};
        runtime.register_adapter(adapter);

        const auto command = runtime.run_frame(liquid::FrameInput{
            0, {}, {simulated_effect("sim.failure", "office", 70)}}
        ).commands.front();
        REQUIRE(runtime.command_status(command.commandId) ==
                std::optional<liquid::CommandStatus>{liquid::CommandStatus::Failed});
        REQUIRE(!runtime.observed_state(
            liquid::AdapterRoute{"sim.failure"}, liquid::EffectTarget{"office"}));
        REQUIRE(!adapter.state(liquid::EffectTarget{"office"}));
    }

    {
        auto options = simulation_options(21);
        Runtime runtime{options};
        liquid::simulation::AdapterBehavior behavior;
        behavior.silent = true;
        liquid::simulation::InMemoryAdapter adapter{
            liquid::AdapterRoute{"sim.silent"}, behavior};
        runtime.register_adapter(adapter);

        const auto command = runtime.run_frame(liquid::FrameInput{
            0, {}, {simulated_effect("sim.silent", "office", 70)}}
        ).commands.front();
        runtime.run_frame(liquid::FrameInput{30000, {}, {}});
        REQUIRE(runtime.command_status(command.commandId) ==
                std::optional<liquid::CommandStatus>{liquid::CommandStatus::TimedOut});
        REQUIRE(!runtime.observed_state(
            liquid::AdapterRoute{"sim.silent"}, liquid::EffectTarget{"office"}));
        REQUIRE(!adapter.state(liquid::EffectTarget{"office"}));
    }

    {
        auto options = simulation_options(22);
        Runtime runtime{options};
        liquid::simulation::AdapterBehavior behavior;
        behavior.crashDuringDispatch = true;
        liquid::simulation::InMemoryAdapter adapter{
            liquid::AdapterRoute{"sim.crash"},
            behavior,
            liquid::AdapterCapabilities{false, true, false}
        };
        runtime.register_adapter(adapter);

        const auto command = runtime.run_frame(liquid::FrameInput{
            0, {}, {simulated_effect("sim.crash", "office", 70)}}
        ).commands.front();
        REQUIRE(runtime.command_status(command.commandId) ==
                std::optional<liquid::CommandStatus>{
                    liquid::CommandStatus::Indeterminate});
        REQUIRE(runtime.run_frame(liquid::FrameInput{
            1, {}, {simulated_effect("sim.crash", "office", 30)}}
        ).commands.empty());

        runtime.reconcile_indeterminate(
            liquid::AdapterRoute{"sim.crash"},
            liquid::EffectTarget{"office"},
            liquid::Value{std::uint64_t{70}}
        );
        REQUIRE(runtime.command_status(command.commandId) ==
                std::optional<liquid::CommandStatus>{liquid::CommandStatus::Applied});
        REQUIRE(runtime.observed_state(
            liquid::AdapterRoute{"sim.crash"}, liquid::EffectTarget{"office"}) ==
            std::optional<liquid::Value>{liquid::Value{std::uint64_t{70}}});
    }

    {
        auto options = simulation_options(23, liquid::FeedbackTiming::Deferred);
        Runtime runtime{options};
        liquid::simulation::AdapterBehavior behavior;
        behavior.latencyMs = 5;
        behavior.reverseDelivery = true;
        liquid::simulation::InMemoryAdapter adapter{
            liquid::AdapterRoute{"sim.reverse"}, behavior};
        runtime.register_adapter(adapter);

        const auto issued = runtime.run_frame(liquid::FrameInput{
            0,
            {},
            {
                simulated_effect("sim.reverse", "first", 10),
                simulated_effect("sim.reverse", "second", 20)
            }
        });
        REQUIRE(issued.commands.size() == 2);
        REQUIRE(adapter.deliver_through(5) == 2);
        const auto delivered = runtime.run_frame(liquid::FrameInput{5, {}, {}});
        REQUIRE(delivered.reports.size() == 2);
        REQUIRE(delivered.reports.front().commandId == issued.commands.back().commandId);
        REQUIRE(delivered.reports.back().commandId == issued.commands.front().commandId);
        REQUIRE(runtime.observed_state(
            liquid::AdapterRoute{"sim.reverse"}, liquid::EffectTarget{"first"}) ==
            std::optional<liquid::Value>{liquid::Value{std::uint64_t{10}}});
        REQUIRE(runtime.observed_state(
            liquid::AdapterRoute{"sim.reverse"}, liquid::EffectTarget{"second"}) ==
            std::optional<liquid::Value>{liquid::Value{std::uint64_t{20}}});
    }
}

TEST_CASE("simulator remains bounded across 1000 frames and 5000 effect targets") {
    auto options = simulation_options(24);
    options.maxRetainedCommands = 64;
    Runtime runtime{options};
    liquid::simulation::InMemoryAdapter adapter{
        liquid::AdapterRoute{"sim.stress"}};
    runtime.register_adapter(adapter);

    std::vector<liquid::ResolvedEffect> effects;
    effects.reserve(5000);
    for (std::size_t index = 0; index < 5000; ++index) {
        effects.push_back(simulated_effect(
            "sim.stress",
            "target-" + std::to_string(index),
            static_cast<std::uint64_t>(index % 101)
        ));
    }

    const auto first = runtime.run_frame(liquid::FrameInput{0, {}, effects});
    REQUIRE(first.commands.size() == effects.size());
    const auto firstCommand = first.commands.front().commandId;
    const auto lastCommand = first.commands.back().commandId;
    const auto unchanged = runtime.run_frame(liquid::FrameInput{1, {}, effects});
    REQUIRE(unchanged.commands.empty());
    for (std::uint64_t frame = 2; frame < 1000; ++frame)
        REQUIRE(runtime.run_frame(liquid::FrameInput{frame, {}, {}}).commands.empty());

    REQUIRE(runtime.frame() == liquid::FrameNumber{1000});
    REQUIRE(!runtime.command_status(firstCommand));
    REQUIRE(runtime.command_status(lastCommand) ==
            std::optional<liquid::CommandStatus>{liquid::CommandStatus::Applied});
    REQUIRE(adapter.pending() == 0);
    REQUIRE(adapter.state(liquid::EffectTarget{"target-0"}) ==
            std::optional<liquid::Value>{liquid::Value{std::uint64_t{0}}});
    REQUIRE(adapter.state(liquid::EffectTarget{"target-4999"}) ==
            std::optional<liquid::Value>{liquid::Value{std::uint64_t{50}}});
    REQUIRE(runtime.observed_state(
        liquid::AdapterRoute{"sim.stress"}, liquid::EffectTarget{"target-0"}) ==
        adapter.state(liquid::EffectTarget{"target-0"}));
    REQUIRE(runtime.observed_state(
        liquid::AdapterRoute{"sim.stress"}, liquid::EffectTarget{"target-4999"}) ==
        adapter.state(liquid::EffectTarget{"target-4999"}));
}

TEST_CASE("simulator publishes revisioned unsolicited device observations") {
    auto options = simulation_options(26, liquid::FeedbackTiming::Deferred);
    Runtime runtime{options};
    liquid::simulation::InMemoryAdapter adapter{
        liquid::AdapterRoute{"sim.observed"}};
    runtime.register_adapter(adapter);

    const liquid::EffectTarget target{"office"};
    REQUIRE(adapter.observe(
        liquid::SessionId{26},
        target,
        liquid::Value{std::uint64_t{10}},
        0,
        5,
        runtime.feedback_sender()) == liquid::StateRevision{1});
    REQUIRE(adapter.deliver_through(4) == 0);
    REQUIRE(adapter.deliver_through(5) == 1);

    const auto observed = runtime.run_frame(liquid::FrameInput{5, {}, {}});
    REQUIRE(observed.observations.size() == 1);
    REQUIRE(runtime.observed_state(
        liquid::AdapterRoute{"sim.observed"}, target) ==
        std::optional<liquid::Value>{liquid::Value{std::uint64_t{10}}});

    const auto issued = runtime.run_frame(liquid::FrameInput{
        6, {}, {simulated_effect("sim.observed", "office", 30)}});
    REQUIRE(issued.commands.size() == 1);
    REQUIRE(adapter.revision(target) ==
        std::optional<liquid::StateRevision>{liquid::StateRevision{2}});
    REQUIRE(runtime.run_frame(liquid::FrameInput{7, {}, {}}).reports.size() == 1);
    REQUIRE(runtime.observed_state(
        liquid::AdapterRoute{"sim.observed"}, target) ==
        std::optional<liquid::Value>{liquid::Value{std::uint64_t{30}}});

    REQUIRE(adapter.observe(
        liquid::SessionId{26},
        target,
        liquid::Value{std::uint64_t{20}},
        8,
        8,
        runtime.feedback_sender()) == liquid::StateRevision{3});
    REQUIRE(adapter.deliver_through(8) == 1);
    REQUIRE(runtime.run_frame(liquid::FrameInput{8, {}, {}}).observations.size() == 1);
    REQUIRE(runtime.observed_state(
        liquid::AdapterRoute{"sim.observed"}, target) ==
        std::optional<liquid::Value>{liquid::Value{std::uint64_t{20}}});
}

TEST_CASE("durable simulator records project and verify without the runtime") {
    TemporarySimulationFile file;
    liquid::EventStoreMetadata metadata;
    metadata.session = liquid::SessionId{25};
    metadata.engineVersion = "0.1.0-test";
    metadata.feedbackTiming = liquid::FeedbackTiming::Immediate;
    std::vector<liquid::EventRecord> recorded;

    {
        liquid::FileEventStore store{file.path(), metadata};
        auto options = simulation_options(25);
        options.allowVolatileEffects = false;
        options.eventStore = &store;
        liquid::simulation::InMemoryAdapter adapter{
            liquid::AdapterRoute{"sim.durable"}};
        Runtime runtime{options};
        runtime.register_adapter(adapter);

        REQUIRE(runtime.run_frame(liquid::FrameInput{
            10, {}, {simulated_effect("sim.durable", "office", 70)}}
        ).commands.size() == 1);
        REQUIRE(runtime.run_frame(liquid::FrameInput{
            11, {}, {simulated_effect("sim.durable", "office", 70)}}
        ).commands.empty());
        REQUIRE(runtime.run_frame(liquid::FrameInput{
            12, {}, {simulated_effect("sim.durable", "office", 30)}}
        ).commands.size() == 1);

        recorded = store.read_all();
        liquid::ReplayProjector projector;
        const auto projected = projector.project(store.metadata(), recorded);
        REQUIRE(projected.replayPosition == recorded.back().sequence);
        REQUIRE(projected.observedState.at("sim.durable:office") ==
                liquid::Value{std::uint64_t{30}});
        REQUIRE(projected.commandAttempts.size() == 2);
        REQUIRE(projected.reports.size() == 2);
        REQUIRE(projected.frameEvents.size() == 6);

        liquid::ReplayVerifier verifier;
        std::size_t attempted = 0;
        std::size_t reported = 0;
        const auto divergence = verifier.verify(
            recorded,
            [&](const liquid::EventRecord& evidence) {
                if (evidence.type == liquid::EventType::CommandAttempted)
                    ++attempted;
                if (evidence.type == liquid::EventType::ReportReceived)
                    ++reported;
                return std::vector<liquid::EventRecord>{evidence};
            }
        );
        REQUIRE(!divergence);
        REQUIRE(attempted == 2);
        REQUIRE(reported == 2);
    }

    liquid::FileEventStore reopened{file.path(), metadata};
    REQUIRE(reopened.read_all() == recorded);
    liquid::ReplayProjector projector;
    const auto replayed = projector.project(reopened.metadata(), reopened.read_all());
    REQUIRE(replayed.observedState.at("sim.durable:office") ==
            liquid::Value{std::uint64_t{30}});
}
