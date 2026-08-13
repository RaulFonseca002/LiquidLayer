#pragma once

#include "liquid/effects/EffectAdapter.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <utility>

namespace liquid::simulation {

struct AdapterBehavior {
    std::uint64_t latencyMs = 0;
    CommandStatus outcome = CommandStatus::Applied;
    std::size_t duplicateReports = 0;
    bool silent = false;
    bool reverseDelivery = false;
    bool crashDuringDispatch = false;
    std::function<Value(const Value&)> normalize;
};

struct AdapterLimits {
    std::size_t maxDuplicateReports = 64;
    std::size_t maxPendingReports = 4096;
    std::size_t maxCachedOutcomes = 4096;
};

class InMemoryAdapter final : public EffectAdapter {
    using CommandKey = std::pair<std::uint64_t, std::uint64_t>;

    struct PendingReport {
        EffectReport report;
        FeedbackSender sender;
    };

    struct CachedOutcome {
        EffectCommand command;
        DispatchResult result;
    };

    AdapterRoute adapterRoute;
    AdapterCapabilities adapterCapabilities;
    AdapterBehavior adapterBehavior;
    AdapterLimits adapterLimits;
    std::multimap<std::uint64_t, PendingReport> pendingReports;
    std::map<CommandKey, CachedOutcome> cachedOutcomes;
    std::map<std::string, Value> deviceState;

    void validate_behavior(const AdapterBehavior& behavior) const;
    DispatchResult cache_outcome(
        const EffectCommand& command,
        DispatchResult result
    );

public:
    explicit InMemoryAdapter(
        AdapterRoute route,
        AdapterBehavior behavior = {},
        AdapterCapabilities capabilities = {false, true, true},
        AdapterLimits limits = {}
    );

    const AdapterRoute& route() const override;
    AdapterCapabilities capabilities() const override;
    DispatchResult dispatch(
        const EffectCommand& command,
        FeedbackSender feedback
    ) override;

    std::size_t deliver_through(std::uint64_t now);
    void set_behavior(AdapterBehavior behavior);
    std::optional<Value> state(const EffectTarget& target) const;
    std::size_t pending() const;
};

}
