#include "liquid/simulation/InMemoryAdapter.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace liquid::simulation {

InMemoryAdapter::InMemoryAdapter(
    AdapterRoute route,
    AdapterBehavior behavior,
    AdapterCapabilities capabilitiesValue,
    AdapterLimits limits
) : adapterRoute(std::move(route)),
    adapterCapabilities(capabilitiesValue),
    adapterBehavior(std::move(behavior)),
    adapterLimits(limits) {
    if (adapterCapabilities.nativeIdempotency &&
        adapterLimits.maxCachedOutcomes == 0) {
        throw std::invalid_argument(
            "native idempotency requires a positive outcome-cache capacity"
        );
    }
    validate_behavior(adapterBehavior);
}

void InMemoryAdapter::validate_behavior(const AdapterBehavior& behavior) const {
    if (behavior.duplicateReports > adapterLimits.maxDuplicateReports ||
        behavior.duplicateReports == std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument("simulated duplicate-report limit exceeded");
    }
    if (behavior.outcome == CommandStatus::Pending)
        throw std::invalid_argument("pending is not a simulated report outcome");
}

DispatchResult InMemoryAdapter::cache_outcome(
    const EffectCommand& command,
    DispatchResult result
) {
    if (!adapterCapabilities.nativeIdempotency)
        return result;

    const CommandKey key{command.sessionId.value, command.commandId.value};
    const auto [position, inserted] = cachedOutcomes.emplace(
        key,
        CachedOutcome{command, result}
    );
    if (!inserted)
        throw std::logic_error("simulator outcome cache insertion collided");
    return position->second.result;
}

const AdapterRoute& InMemoryAdapter::route() const {
    return adapterRoute;
}

AdapterCapabilities InMemoryAdapter::capabilities() const {
    return adapterCapabilities;
}

DispatchResult InMemoryAdapter::dispatch(
    const EffectCommand& command,
    FeedbackSender feedback
) {
    validate_effect_command(command);
    if (command.effect.adapterRoute != adapterRoute)
        return DispatchResult::rejected("command route does not match simulator");

    if (adapterCapabilities.nativeIdempotency) {
        const CommandKey key{command.sessionId.value, command.commandId.value};
        const auto cached = cachedOutcomes.find(key);
        if (cached != cachedOutcomes.end()) {
            if (cached->second.command != command) {
                throw std::invalid_argument(
                    "simulator command ID was reused for a different command"
                );
            }
            return cached->second.result;
        }
        if (cachedOutcomes.size() >= adapterLimits.maxCachedOutcomes) {
            return DispatchResult::failed(
                "simulator idempotency outcome cache capacity exceeded"
            );
        }
    }

    if (adapterBehavior.crashDuringDispatch)
        throw std::runtime_error("simulated adapter crash uncertainty");
    if (adapterBehavior.silent)
        return cache_outcome(command, DispatchResult::accepted());

    if (adapterBehavior.latencyMs >
        std::numeric_limits<std::uint64_t>::max() - command.issuedAtMs) {
        return cache_outcome(command, DispatchResult::failed(
            "simulated report timestamp overflow"
        ));
    }

    const std::size_t totalPending = pendingReports.size() + pendingObservations.size();
    const std::size_t requiredPending = adapterBehavior.latencyMs == 0
        ? adapterBehavior.duplicateReports
        : adapterBehavior.duplicateReports + 1;
    if (totalPending > adapterLimits.maxPendingReports ||
        requiredPending > adapterLimits.maxPendingReports - totalPending) {
        return cache_outcome(command, DispatchResult::failed(
            "simulated pending-report capacity exceeded"
        ));
    }

    std::optional<Value> observed;
    StateRevision stateRevision{command.commandId.value};
    if (adapterBehavior.outcome == CommandStatus::Applied) {
        observed = adapterBehavior.normalize
            ? adapterBehavior.normalize(command.effect.desiredValue)
            : command.effect.desiredValue;
        observed->validate();
        auto revision = deviceRevisions.find(command.effect.target.value());
        if (revision == deviceRevisions.end()) {
            stateRevision = StateRevision{1};
        } else {
            if (revision->second.value == std::numeric_limits<std::uint64_t>::max())
                return cache_outcome(command, DispatchResult::failed(
                    "simulated device state revision exhausted"));
            stateRevision = StateRevision{revision->second.value + 1};
        }
        deviceRevisions.insert_or_assign(
            command.effect.target.value(), stateRevision);
        deviceState.insert_or_assign(command.effect.target.value(), *observed);
    }

    EffectReport report{
        command.sessionId,
        command.commandId,
        command.effect.adapterRoute,
        command.effect.target,
        adapterBehavior.outcome,
        std::move(observed),
        {},
        command.issuedAtMs + adapterBehavior.latencyMs,
        stateRevision
    };
    validate_effect_report(report);

    if (adapterBehavior.latencyMs == 0) {
        for (std::size_t duplicate = 0;
             duplicate < adapterBehavior.duplicateReports;
             ++duplicate) {
            const FeedbackSendResult result = feedback.try_send(report);
            if (result == FeedbackSendResult::Full) {
                pendingReports.emplace(
                    report.reportedAtMs,
                    PendingReport{report, feedback}
                );
            }
        }
        return cache_outcome(
            command,
            DispatchResult::accepted(std::move(report))
        );
    }

    const std::size_t reportCount = adapterBehavior.duplicateReports + 1;
    for (std::size_t index = 0; index < reportCount; ++index) {
        pendingReports.emplace(
            report.reportedAtMs,
            PendingReport{report, feedback}
        );
    }
    return cache_outcome(command, DispatchResult::accepted());
}

std::size_t InMemoryAdapter::deliver_through(std::uint64_t now) {
    std::size_t delivered = 0;
    while (!pendingReports.empty()) {
        auto found = pendingReports.begin();
        if (adapterBehavior.reverseDelivery) {
            found = pendingReports.upper_bound(now);
            if (found == pendingReports.begin())
                return delivered;
            --found;
        } else {
            if (found->first > now)
                return delivered;
        }

        const FeedbackSendResult result = found->second.sender.try_send(
            found->second.report
        );
        if (result == FeedbackSendResult::Full)
            return delivered;
        if (result == FeedbackSendResult::Sent)
            ++delivered;
        pendingReports.erase(found);
    }

    while (!pendingObservations.empty()) {
        auto found = pendingObservations.begin();
        if (adapterBehavior.reverseDelivery) {
            found = pendingObservations.upper_bound(now);
            if (found == pendingObservations.begin())
                return delivered;
            --found;
        } else if (found->first > now) {
            return delivered;
        }
        const FeedbackSendResult result = found->second.sender.try_send(
            found->second.observation);
        if (result == FeedbackSendResult::Full)
            return delivered;
        if (result == FeedbackSendResult::Sent)
            ++delivered;
        pendingObservations.erase(found);
    }
    return delivered;
}

StateRevision InMemoryAdapter::observe(
    SessionId session,
    const EffectTarget& target,
    Value value,
    std::uint64_t observedAtMs,
    std::uint64_t deliverAtMs,
    FeedbackSender feedback
) {
    if (!session.valid())
        throw std::invalid_argument("simulated observation session is invalid");
    if (target.value().empty())
        throw std::invalid_argument("simulated observation target is empty");
    if (deliverAtMs < observedAtMs)
        throw std::invalid_argument("simulated observation cannot arrive before it was observed");
    value.validate();
    const std::size_t totalPending = pendingReports.size() + pendingObservations.size();
    if (totalPending >= adapterLimits.maxPendingReports)
        throw std::overflow_error("simulated pending-report capacity exceeded");

    StateRevision next{1};
    const auto prior = deviceRevisions.find(target.value());
    if (prior != deviceRevisions.end()) {
        if (prior->second.value == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("simulated device state revision exhausted");
        next = StateRevision{prior->second.value + 1};
    }
    deviceState.insert_or_assign(target.value(), value);
    deviceRevisions.insert_or_assign(target.value(), next);
    ExternalObservation observation{
        session,
        adapterRoute,
        target,
        std::move(value),
        next,
        observedAtMs};
    validate_external_observation(observation);
    pendingObservations.emplace(
        deliverAtMs, PendingObservation{std::move(observation), std::move(feedback)});
    return next;
}

void InMemoryAdapter::set_behavior(AdapterBehavior behavior) {
    validate_behavior(behavior);
    adapterBehavior = std::move(behavior);
}

std::optional<Value> InMemoryAdapter::state(const EffectTarget& target) const {
    const auto found = deviceState.find(target.value());
    if (found == deviceState.end())
        return std::nullopt;
    return found->second;
}

std::optional<StateRevision> InMemoryAdapter::revision(
    const EffectTarget& target
) const {
    const auto found = deviceRevisions.find(target.value());
    if (found == deviceRevisions.end())
        return std::nullopt;
    return found->second;
}

std::size_t InMemoryAdapter::pending() const {
    return pendingReports.size() + pendingObservations.size();
}

}
