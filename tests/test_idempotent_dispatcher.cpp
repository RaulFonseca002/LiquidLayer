#include "liquid/effects/IdempotentDispatcher.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

using namespace liquid;

namespace {

liquid::EffectCommand command(std::uint64_t commandId, std::uint64_t desired = 70) {
    return liquid::EffectCommand{
        liquid::SessionId{9},
        liquid::CommandId{commandId},
        liquid::ResolvedEffect{
            liquid::AdapterRoute{"test.adapter"},
            liquid::EffectTarget{"light.office"},
            liquid::Value(desired)
        },
        100
    };
}

class CountingAdapter final : public liquid::EffectAdapter {
    liquid::AdapterRoute adapterRoute{"test.adapter"};

public:
    std::atomic<int> calls{0};

    const liquid::AdapterRoute& route() const override {
        return adapterRoute;
    }

    liquid::AdapterCapabilities capabilities() const override {
        return {true, true, true};
    }

    liquid::DispatchResult dispatch(
        const liquid::EffectCommand& effectCommand,
        liquid::FeedbackSender
    ) override {
        ++calls;
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
    }
};

class BlockingAdapter final : public liquid::EffectAdapter {
    liquid::AdapterRoute adapterRoute{"test.adapter"};
    std::mutex mutex;
    std::condition_variable condition;
    bool released = false;

public:
    std::atomic<int> calls{0};
    std::atomic<int> active{0};
    std::atomic<int> maximumActive{0};
    bool throwAfterRelease = false;

    const liquid::AdapterRoute& route() const override {
        return adapterRoute;
    }

    liquid::AdapterCapabilities capabilities() const override {
        return {false, false, false};
    }

    liquid::DispatchResult dispatch(
        const liquid::EffectCommand&,
        liquid::FeedbackSender
    ) override {
        ++calls;
        const int currentActive = ++active;
        int previousMaximum = maximumActive.load();
        while (currentActive > previousMaximum &&
               !maximumActive.compare_exchange_weak(previousMaximum, currentActive)) {
        }

        std::unique_lock lock(mutex);
        condition.wait(lock, [&] { return released; });
        --active;
        if (throwAfterRelease)
            throw std::runtime_error("uncertain dispatch");
        return liquid::DispatchResult::accepted();
    }

    void wait_for_calls(int expected) {
        while (calls.load() < expected)
            std::this_thread::yield();
    }

    void release() {
        {
            std::lock_guard lock(mutex);
            released = true;
        }
        condition.notify_all();
    }
};

class MemoryPersistentCache final : public liquid::PersistentOutcomeCache {
    using Key = std::pair<std::uint64_t, std::uint64_t>;
    std::mutex mutex;
    std::map<Key, liquid::CachedDispatchOutcome> outcomes;

    static Key key(liquid::SessionId sessionId, liquid::CommandId commandId) {
        return {sessionId.value, commandId.value};
    }

public:
    std::optional<liquid::CachedDispatchOutcome> find(
        liquid::SessionId sessionId,
        liquid::CommandId commandId
    ) override {
        std::lock_guard lock(mutex);
        const auto found = outcomes.find(key(sessionId, commandId));
        if (found == outcomes.end())
            return std::nullopt;
        return found->second;
    }

    void store(const liquid::CachedDispatchOutcome& outcome) override {
        std::lock_guard lock(mutex);
        outcomes.insert_or_assign(
            key(outcome.command.sessionId, outcome.command.commandId),
            outcome
        );
    }
};

}

TEST_CASE("test_idempotent_dispatcher") {
    auto feedback = liquid::make_feedback_channel(4);
    CountingAdapter adapter;
    liquid::IdempotentDispatcher dispatcher(2);

    const auto first = dispatcher.dispatch(adapter, command(1), feedback.sender);
    const auto duplicate = dispatcher.dispatch(adapter, command(1), feedback.sender);
    REQUIRE(first == duplicate);
    REQUIRE(adapter.calls == 1);
    REQUIRE(dispatcher.cached_outcomes() == 1);

    bool conflictRejected = false;
    try {
        dispatcher.dispatch(adapter, command(1, 30), feedback.sender);
    } catch (const std::invalid_argument&) {
        conflictRejected = true;
    }
    REQUIRE(conflictRejected);
    REQUIRE(adapter.calls == 1);

    dispatcher.dispatch(adapter, command(2), feedback.sender);
    dispatcher.dispatch(adapter, command(3), feedback.sender);
    REQUIRE(dispatcher.cached_outcomes() == 2);
    dispatcher.dispatch(adapter, command(1), feedback.sender);
    REQUIRE(adapter.calls == 4);

    auto persistent = std::make_shared<MemoryPersistentCache>();
    liquid::IdempotentDispatcher firstProcess(1, persistent);
    firstProcess.dispatch(adapter, command(10), feedback.sender);
    const int afterFirstProcess = adapter.calls.load();

    liquid::IdempotentDispatcher secondProcess(1, persistent);
    secondProcess.dispatch(adapter, command(10), feedback.sender);
    REQUIRE(adapter.calls == afterFirstProcess);

    CountingAdapter concurrentAdapter;
    liquid::IdempotentDispatcher concurrentDispatcher(4);
    std::vector<std::thread> threads;
    for (int index = 0; index < 12; ++index) {
        threads.emplace_back([&] {
            const auto result = concurrentDispatcher.dispatch(
                concurrentAdapter,
                command(20),
                feedback.sender
            );
            REQUIRE(result.disposition == liquid::DispatchDisposition::Accepted);
        });
    }
    for (auto& thread : threads)
        thread.join();
    REQUIRE(concurrentAdapter.calls == 1);

    {
        BlockingAdapter uncertainAdapter;
        uncertainAdapter.throwAfterRelease = true;
        liquid::IdempotentDispatcher uncertainDispatcher(8);
        std::atomic<int> started{0};
        std::atomic<int> failures{0};
        std::vector<std::thread> duplicates;
        for (int index = 0; index < 8; ++index) {
            duplicates.emplace_back([&] {
                ++started;
                try {
                    uncertainDispatcher.dispatch(
                        uncertainAdapter,
                        command(30),
                        feedback.sender
                    );
                } catch (const std::runtime_error&) {
                    ++failures;
                }
            });
        }
        while (started.load() != 8 || uncertainAdapter.calls.load() != 1)
            std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        uncertainAdapter.release();
        for (auto& duplicateThread : duplicates)
            duplicateThread.join();
        REQUIRE(failures == 8);
        REQUIRE(uncertainAdapter.calls == 1);

        bool conflictingUncertainReuse = false;
        try {
            uncertainDispatcher.dispatch(
                uncertainAdapter,
                command(30, 30),
                feedback.sender
            );
        } catch (const std::invalid_argument&) {
            conflictingUncertainReuse = true;
        }
        REQUIRE(conflictingUncertainReuse);
        REQUIRE(uncertainAdapter.calls == 1);
    }

    {
        BlockingAdapter boundedAdapter;
        liquid::IdempotentDispatcher boundedDispatcher(4, {}, 1);
        std::atomic<bool> secondStarted{false};
        std::thread firstDispatch([&] {
            boundedDispatcher.dispatch(boundedAdapter, command(40), feedback.sender);
        });
        boundedAdapter.wait_for_calls(1);
        std::thread secondDispatch([&] {
            secondStarted = true;
            boundedDispatcher.dispatch(boundedAdapter, command(41), feedback.sender);
        });
        while (!secondStarted.load())
            std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        REQUIRE(boundedAdapter.calls == 1);
        REQUIRE(boundedAdapter.maximumActive == 1);
        boundedAdapter.release();
        firstDispatch.join();
        secondDispatch.join();
        REQUIRE(boundedAdapter.calls == 2);
        REQUIRE(boundedAdapter.maximumActive == 1);
    }

    bool invalidCapacity = false;
    try {
        liquid::IdempotentDispatcher invalid(0);
    } catch (const std::invalid_argument&) {
        invalidCapacity = true;
    }
    REQUIRE(invalidCapacity);

}
