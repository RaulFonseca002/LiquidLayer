#include "liquid/effects/Feedback.hpp"

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <cstdint>
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
