#pragma once

#include "liquid/effects/EffectAdapter.hpp"

#include <cstddef>
#include <memory>
#include <optional>

namespace liquid {

struct CachedDispatchOutcome {
    EffectCommand command;
    DispatchResult result;

    bool operator==(const CachedDispatchOutcome&) const = default;
};

class PersistentOutcomeCache {
public:
    virtual ~PersistentOutcomeCache() = default;

    virtual std::optional<CachedDispatchOutcome> find(
        SessionId sessionId,
        CommandId commandId
    ) = 0;
    virtual void store(const CachedDispatchOutcome& outcome) = 0;
};

class IdempotentDispatcher {
    class Impl;
    std::unique_ptr<Impl> implementation;

public:
    explicit IdempotentDispatcher(
        std::size_t maxMemoryOutcomes,
        std::shared_ptr<PersistentOutcomeCache> persistentCache = {},
        std::size_t maxConcurrentDispatches = 0
    );
    ~IdempotentDispatcher();

    IdempotentDispatcher(const IdempotentDispatcher&) = delete;
    IdempotentDispatcher& operator=(const IdempotentDispatcher&) = delete;
    IdempotentDispatcher(IdempotentDispatcher&&) noexcept;
    IdempotentDispatcher& operator=(IdempotentDispatcher&&) noexcept;

    DispatchResult dispatch(
        EffectAdapter& adapter,
        const EffectCommand& command,
        FeedbackSender feedback
    );

    std::size_t cached_outcomes() const;
    void clear_memory();
};

}
