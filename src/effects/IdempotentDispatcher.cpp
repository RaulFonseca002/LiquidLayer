#include "liquid/effects/IdempotentDispatcher.hpp"

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <list>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace liquid {

namespace {

struct DispatchKey {
    SessionId sessionId;
    CommandId commandId;

    bool operator==(const DispatchKey&) const = default;
};

struct DispatchKeyHash {
    std::size_t operator()(const DispatchKey& key) const {
        const auto first = std::hash<std::uint64_t>{}(key.sessionId.value);
        const auto second = std::hash<std::uint64_t>{}(key.commandId.value);
        return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
    }
};

DispatchKey key_for(const EffectCommand& command) {
    return {command.sessionId, command.commandId};
}

void require_same_command(
    const CachedDispatchOutcome& cached,
    const EffectCommand& command
) {
    if (cached.command != command)
        throw std::invalid_argument(
            "an idempotency key was reused for a different effect command"
        );
    validate_dispatch_result_for_command(cached.result, command);
}

}

class IdempotentDispatcher::Impl {
    struct MemoryEntry {
        CachedDispatchOutcome outcome;
        std::list<DispatchKey>::iterator recency;
    };

    struct InFlightEntry {
        bool completed = false;
        std::exception_ptr failure;
    };

    struct UncertainOutcome {
        EffectCommand command;
        std::exception_ptr failure;
    };

    std::size_t maximumMemoryOutcomes;
    std::size_t maximumConcurrentDispatches;
    std::shared_ptr<PersistentOutcomeCache> persistentCache;
    mutable std::mutex mutex;
    std::mutex persistentMutex;
    std::condition_variable completed;
    std::list<DispatchKey> recency;
    std::unordered_map<DispatchKey, MemoryEntry, DispatchKeyHash> memory;
    std::unordered_map<
        DispatchKey,
        std::shared_ptr<InFlightEntry>,
        DispatchKeyHash
    > inFlight;
    std::unordered_map<DispatchKey, UncertainOutcome, DispatchKeyHash> uncertain;

    std::optional<CachedDispatchOutcome> find_memory_locked(const DispatchKey& key) {
        const auto found = memory.find(key);
        if (found == memory.end())
            return std::nullopt;

        recency.splice(recency.begin(), recency, found->second.recency);
        return found->second.outcome;
    }

    void finish_failure(
        const DispatchKey& key,
        const std::shared_ptr<InFlightEntry>& entry,
        const EffectCommand& command,
        const std::exception_ptr& error
    ) {
        {
            std::lock_guard lock(mutex);
            try {
                if (!uncertain.contains(key) &&
                    uncertain.size() >= maximumMemoryOutcomes) {
                    uncertain.erase(uncertain.begin());
                }
                uncertain.insert_or_assign(
                    key,
                    UncertainOutcome{command, error}
                );
            } catch (...) {
            }
            entry->failure = error;
            entry->completed = true;
            inFlight.erase(key);
        }
        completed.notify_all();
    }

    void remember(
        const DispatchKey& key,
        const std::shared_ptr<InFlightEntry>& entry,
        const EffectCommand& command,
        CachedDispatchOutcome outcome
    ) {
        try {
            std::lock_guard lock(mutex);
            recency.push_front(key);
            try {
                memory.emplace(
                    key,
                    MemoryEntry{std::move(outcome), recency.begin()}
                );
            } catch (...) {
                recency.pop_front();
                throw;
            }

            while (memory.size() > maximumMemoryOutcomes) {
                const auto expired = recency.back();
                memory.erase(expired);
                recency.pop_back();
            }
            entry->completed = true;
            inFlight.erase(key);
        } catch (...) {
            finish_failure(key, entry, command, std::current_exception());
            throw;
        }
        completed.notify_all();
    }

public:
    Impl(
        std::size_t maxMemoryOutcomes,
        std::shared_ptr<PersistentOutcomeCache> cache,
        std::size_t maxConcurrentDispatches
    ) :
        maximumMemoryOutcomes(maxMemoryOutcomes),
        maximumConcurrentDispatches(maxConcurrentDispatches),
        persistentCache(std::move(cache)) {}

    DispatchResult dispatch(
        EffectAdapter& adapter,
        const EffectCommand& command,
        FeedbackSender feedback
    ) {
        validate_effect_command(command);
        if (adapter.route() != command.effect.adapterRoute)
            throw std::invalid_argument("effect command does not match adapter route");

        const auto key = key_for(command);
        std::shared_ptr<InFlightEntry> inFlightEntry;
        {
            std::unique_lock lock(mutex);
            while (true) {
                if (auto cached = find_memory_locked(key)) {
                    require_same_command(*cached, command);
                    return cached->result;
                }
                if (const auto failed = uncertain.find(key);
                    failed != uncertain.end()) {
                    if (failed->second.command != command) {
                        throw std::invalid_argument(
                            "an uncertain idempotency key was reused for a different command"
                        );
                    }
                    std::rethrow_exception(failed->second.failure);
                }
                const auto running = inFlight.find(key);
                if (running != inFlight.end()) {
                    const auto duplicateEntry = running->second;
                    completed.wait(lock, [&] { return duplicateEntry->completed; });
                    if (duplicateEntry->failure)
                        std::rethrow_exception(duplicateEntry->failure);
                    continue;
                }
                if (inFlight.size() >= maximumConcurrentDispatches) {
                    completed.wait(lock, [&] {
                        return inFlight.size() < maximumConcurrentDispatches ||
                               inFlight.contains(key) || memory.contains(key) ||
                               uncertain.contains(key);
                    });
                    continue;
                }
                inFlightEntry = std::make_shared<InFlightEntry>();
                if (inFlight.emplace(key, inFlightEntry).second)
                    break;
            }
        }

        try {
            if (persistentCache) {
                std::optional<CachedDispatchOutcome> persistent;
                {
                    std::lock_guard lock(persistentMutex);
                    persistent = persistentCache->find(command.sessionId, command.commandId);
                }
                if (persistent) {
                    require_same_command(*persistent, command);
                    const auto result = persistent->result;
                    remember(key, inFlightEntry, command, std::move(*persistent));
                    return result;
                }
            }

            auto result = adapter.dispatch(command, std::move(feedback));
            validate_dispatch_result_for_command(result, command);
            CachedDispatchOutcome outcome{command, result};

            if (persistentCache) {
                std::lock_guard lock(persistentMutex);
                persistentCache->store(outcome);
            }

            remember(key, inFlightEntry, command, std::move(outcome));
            return result;
        } catch (...) {
            const std::exception_ptr error = std::current_exception();
            if (!inFlightEntry->completed)
                finish_failure(key, inFlightEntry, command, error);
            std::rethrow_exception(error);
        }
    }

    std::size_t cached_outcomes() const {
        std::lock_guard lock(mutex);
        return memory.size();
    }

    void clear_memory() {
        std::lock_guard lock(mutex);
        memory.clear();
        recency.clear();
        uncertain.clear();
    }
};

IdempotentDispatcher::IdempotentDispatcher(
    std::size_t maxMemoryOutcomes,
    std::shared_ptr<PersistentOutcomeCache> persistentCache,
    std::size_t maxConcurrentDispatches
) {
    if (maxMemoryOutcomes == 0)
        throw std::invalid_argument("idempotent dispatcher capacity must be positive");
    if (maxConcurrentDispatches == 0)
        maxConcurrentDispatches = maxMemoryOutcomes;
    implementation = std::make_unique<Impl>(
        maxMemoryOutcomes,
        std::move(persistentCache),
        maxConcurrentDispatches
    );
}

IdempotentDispatcher::~IdempotentDispatcher() = default;
IdempotentDispatcher::IdempotentDispatcher(IdempotentDispatcher&&) noexcept = default;
IdempotentDispatcher& IdempotentDispatcher::operator=(
    IdempotentDispatcher&&
) noexcept = default;

DispatchResult IdempotentDispatcher::dispatch(
    EffectAdapter& adapter,
    const EffectCommand& command,
    FeedbackSender feedback
) {
    return implementation->dispatch(adapter, command, std::move(feedback));
}

std::size_t IdempotentDispatcher::cached_outcomes() const {
    return implementation->cached_outcomes();
}

void IdempotentDispatcher::clear_memory() {
    implementation->clear_memory();
}

}
