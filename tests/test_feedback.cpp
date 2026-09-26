#include "liquid/effects/Feedback.hpp"

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace liquid;

namespace {

liquid::EffectReport report(std::uint64_t command) {
    return liquid::EffectReport{
        liquid::SessionId{1},
        liquid::CommandId{command},
        liquid::AdapterRoute{"test.adapter"},
        liquid::EffectTarget{"light.office"},
        liquid::CommandStatus::Applied,
        liquid::Value(std::uint64_t{command}),
        {},
        command
    };
}

liquid::ExternalObservation observation(std::uint64_t value) {
    return liquid::ExternalObservation{
        liquid::SessionId{1},
        liquid::AdapterRoute{"test.adapter"},
        liquid::EffectTarget{"light.office"},
        liquid::Value(std::uint64_t{value}),
        liquid::StateRevision{value},
        value
    };
}

}

TEST_CASE("test_feedback") {
    using liquid::FeedbackSendResult;

    const liquid::RetryPolicy retryPolicy;
    retryPolicy.validate();
    REQUIRE(retryPolicy.delay_before_retry(0) == std::chrono::milliseconds{250});
    REQUIRE(retryPolicy.delay_before_retry(4) == std::chrono::seconds{4});
    REQUIRE(!retryPolicy.delay_before_retry(5));
    REQUIRE(retryPolicy.overallTimeout == std::chrono::seconds{30});

    auto channel = liquid::make_feedback_channel(2);
    REQUIRE(channel.sender.try_send(report(1)) == FeedbackSendResult::Sent);
    REQUIRE(channel.sender.try_send(report(2)) == FeedbackSendResult::Sent);
    REQUIRE(channel.sender.try_send(report(3)) == FeedbackSendResult::Full);
    REQUIRE(channel.receiver.pending() == 2);

    auto first = channel.receiver.try_receive();
    auto second = channel.receiver.try_receive();
    REQUIRE((first && first->commandId == liquid::CommandId{1}));
    REQUIRE((second && second->commandId == liquid::CommandId{2}));
    REQUIRE(!channel.receiver.try_receive());

    auto retainedSender = channel.sender;
    channel.receiver.shutdown();
    REQUIRE(channel.receiver.is_shutdown());
    REQUIRE(retainedSender.try_send(report(4)) == FeedbackSendResult::Closed);

    liquid::FeedbackSender orphanedSender;
    {
        auto temporary = liquid::make_feedback_channel(1);
        orphanedSender = temporary.sender;
    }
    REQUIRE(orphanedSender.try_send(report(5)) == FeedbackSendResult::Closed);

    auto replaced = liquid::make_feedback_channel(1);
    auto replacement = liquid::make_feedback_channel(1);
    const auto replacedSender = replaced.sender;
    replaced.receiver = std::move(replacement.receiver);
    REQUIRE(replacedSender.try_send(report(6)) == FeedbackSendResult::Closed);
    REQUIRE(replacement.sender.try_send(report(7)) == FeedbackSendResult::Sent);
    REQUIRE(replaced.receiver.try_receive()->commandId == liquid::CommandId{7});

    auto concurrent = liquid::make_feedback_channel(400);
    std::atomic<std::size_t> producerFailures{0};
    std::vector<std::thread> producers;
    for (std::uint64_t producer = 0; producer < 4; ++producer) {
        producers.emplace_back([
            sender = concurrent.sender, producer, &producerFailures]() mutable {
            for (std::uint64_t item = 0; item < 100; ++item) {
                const auto result = sender.try_send(report(1000 + producer * 100 + item));
                if (result != FeedbackSendResult::Sent)
                    ++producerFailures;
            }
        });
    }
    for (auto& producer : producers)
        producer.join();

    REQUIRE(producerFailures == 0);
    REQUIRE(concurrent.receiver.pending() == 400);
    REQUIRE(concurrent.receiver.drain().size() == 400);
    REQUIRE(concurrent.receiver.pending() == 0);

    {
        auto closing = liquid::make_feedback_channel(128);
        const auto closingSender = closing.sender;
        std::atomic<bool> started{false};
        std::atomic<bool> sawClosed{false};
        std::thread producer([&] {
            started = true;
            std::uint64_t command = 2000;
            while (true) {
                const auto result = closingSender.try_send(report(command++));
                if (result == FeedbackSendResult::Closed) {
                    sawClosed = true;
                    return;
                }
                if (result == FeedbackSendResult::Full)
                    std::this_thread::yield();
            }
        });
        while (!started.load())
            std::this_thread::yield();
        closing.receiver.shutdown();
        producer.join();
        REQUIRE(sawClosed);
    }

    {
        liquid::FeedbackSender senderDuringDestruction;
        auto destructing = liquid::make_feedback_channel(128);
        senderDuringDestruction = destructing.sender;
        std::atomic<bool> started{false};
        std::atomic<bool> sawClosed{false};
        std::thread producer([&] {
            started = true;
            std::uint64_t command = 3000;
            while (senderDuringDestruction.try_send(report(command++)) !=
                   FeedbackSendResult::Closed) {
                std::this_thread::yield();
            }
            sawClosed = true;
        });
        while (!started.load())
            std::this_thread::yield();
        destructing.receiver = liquid::make_feedback_channel(1).receiver;
        producer.join();
        REQUIRE(sawClosed);
    }

    bool invalidCapacity = false;
    try {
        auto invalid = liquid::make_feedback_channel(0);
        (void)invalid;
    } catch (const std::invalid_argument&) {
        invalidCapacity = true;
    }
    REQUIRE(invalidCapacity);

}

TEST_CASE("feedback channel carries external observations with the same bounds as reports") {
    using liquid::FeedbackSendResult;

    auto channel = liquid::make_feedback_channel(2);
    REQUIRE(channel.sender.try_send(observation(1)) == FeedbackSendResult::Sent);
    REQUIRE(channel.sender.try_send(observation(2)) == FeedbackSendResult::Sent);
    REQUIRE(channel.sender.try_send(observation(3)) == FeedbackSendResult::Full);
    REQUIRE(channel.sender.try_send(report(3)) == FeedbackSendResult::Full);
    REQUIRE(channel.receiver.pending() == 2);

    REQUIRE(!channel.receiver.try_receive());
    const auto first = channel.receiver.try_receive_observation();
    const auto second = channel.receiver.try_receive_observation();
    REQUIRE((first && *first == observation(1)));
    REQUIRE((second && *second == observation(2)));
    REQUIRE(!channel.receiver.try_receive_observation());
    REQUIRE(channel.receiver.pending() == 0);

    REQUIRE(channel.sender.try_send(observation(4)) == FeedbackSendResult::Sent);
    REQUIRE(channel.sender.try_send(observation(5)) == FeedbackSendResult::Sent);
    REQUIRE(channel.receiver.drain().empty());
    const auto drained = channel.receiver.drain_observations();
    REQUIRE((drained == std::vector<liquid::ExternalObservation>{
        observation(4), observation(5)}));
    REQUIRE(channel.receiver.drain_observations().empty());
    REQUIRE(channel.receiver.pending() == 0);

    auto mixed = liquid::make_feedback_channel(2);
    REQUIRE(mixed.sender.try_send(report(1)) == FeedbackSendResult::Sent);
    REQUIRE(mixed.sender.try_send(observation(1)) == FeedbackSendResult::Sent);
    REQUIRE(mixed.sender.try_send(observation(2)) == FeedbackSendResult::Full);
    REQUIRE(mixed.sender.try_send(report(2)) == FeedbackSendResult::Full);
    REQUIRE(mixed.receiver.pending() == 2);
    const auto mixedObservations = mixed.receiver.drain_observations();
    REQUIRE((mixedObservations == std::vector<liquid::ExternalObservation>{
        observation(1)}));
    const auto mixedReports = mixed.receiver.drain();
    REQUIRE(mixedReports.size() == 1);
    REQUIRE(mixedReports.front().commandId == liquid::CommandId{1});

    auto closing = liquid::make_feedback_channel(2);
    const auto closingSender = closing.sender;
    {
        std::optional<liquid::FeedbackReceiver> receiver{
            std::move(closing.receiver)};
        REQUIRE(closingSender.try_send(observation(6)) == FeedbackSendResult::Sent);
        REQUIRE(receiver->try_receive_observation() == observation(6));
        receiver.reset();
    }
    REQUIRE(closingSender.is_closed());
    REQUIRE(closingSender.try_send(observation(7)) == FeedbackSendResult::Closed);

    auto shut = liquid::make_feedback_channel(2);
    shut.receiver.shutdown();
    REQUIRE(shut.sender.try_send(observation(8)) == FeedbackSendResult::Closed);
    REQUIRE(!shut.receiver.try_receive_observation());
    REQUIRE(shut.receiver.drain_observations().empty());
}
