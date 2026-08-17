#include "RuntimeInternals.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace liquid::detail {

RuntimeEffectsState::RuntimeEffectsState(RuntimeOptions options)
    : session(options.sessionId),
      timing(options.feedbackTiming),
      retry(std::move(options.retryPolicy)),
      feedback(make_feedback_channel(options.feedbackCapacity)),
      maximumRetainedCommands(options.maxRetainedCommands),
      maximumDeferredReports(options.feedbackCapacity) {
    if (!session.valid())
        throw std::invalid_argument("runtime requires a valid session ID");
    retry.validate();
    if (maximumRetainedCommands == 0)
        throw std::invalid_argument("retained command capacity must be positive");

    if (options.eventStore) {
        store = options.eventStore;
    } else {
        if (!options.allowVolatileEffects)
            throw std::invalid_argument(
                "effect dispatch requires an event store; volatile mode must be explicit"
            );
        EventStoreMetadata metadata;
        metadata.session = session;
        metadata.engineVersion = "0.1.0";
        metadata.feedbackTiming = timing;
        ownedStore = std::make_unique<MemoryEventStore>(std::move(metadata));
        store = ownedStore.get();
    }

    if (store->metadata().session != session ||
        store->metadata().feedbackTiming != timing) {
        throw std::invalid_argument(
            "runtime options do not match event-store session metadata"
        );
    }

    const auto existing = store->read_all();
    if (existing.empty()) {
        append(EventType::SessionStarted, event_payload({
            {"session", Value{session.value}},
            {"feedback_timing", Value{timing == FeedbackTiming::Immediate
                ? "immediate" : "deferred"}},
            {"engine_version", Value{"0.1.0"}}
        }));
        store->flush();
    } else {
        restore(existing);
    }
}

void RuntimeEffectsState::prune_terminal_history(bool reserveCommandSlot) {
    const std::size_t targetSize = maximumRetainedCommands -
        static_cast<std::size_t>(reserveCommandSlot);
    while (commands.size() > targetSize &&
           !terminalOrder.empty()) {
        const std::uint64_t id = terminalOrder.front();
        terminalOrder.pop_front();
        const auto found = commands.find(id);
        if (found == commands.end() || active(found->second.status))
            continue;
        const TargetKey key = target_key(
            found->second.command.effect.adapterRoute,
            found->second.command.effect.target);
        const auto latest = latestByTarget.find(key);
        if (latest != latestByTarget.end() && latest->second == id)
            latestByTarget.erase(latest);
        commands.erase(found);
    }
    if (commands.size() > targetSize)
        throw EventStoreError("active command capacity exceeded");
}

// The non-recording half of a status transition: every live status write
// and its terminal bookkeeping must go through here so the restore path in
// RuntimeEffectsRestore.cpp has one contract to mirror.
void RuntimeEffectsState::set_status(CommandState& state, CommandStatus status) {
    const bool becameTerminal = active(state.status) && !active(status);
    state.status = status;
    if (becameTerminal) {
        terminalOrder.push_back(state.command.commandId.value);
    }
}

void RuntimeEffectsState::transition(
    CommandState& state,
    CommandStatus status,
    const std::string& reason
) {
    append(EventType::CommandStatusChanged, event_payload({
        {"key", Value{"command:" + std::to_string(state.command.commandId.value)}},
        {"value", Value{status_name(status)}},
        {"command_id", Value{state.command.commandId.value}},
        {"status", Value{status_name(status)}},
        {"reason", Value{reason}}
    }));
    set_status(state, status);
}

void RuntimeEffectsState::append(EventType type, Value payload,
            Durability durability) {
    EventData data;
    data.type = type;
    data.payload = std::move(payload);
    store->append(std::move(data), durability);
}

void RuntimeEffectsState::record_status(const CommandState& state, const std::string& reason) {
    append(EventType::CommandStatusChanged, event_payload({
        {"key", Value{"command:" + std::to_string(state.command.commandId.value)}},
        {"value", Value{status_name(state.status)}},
        {"command_id", Value{state.command.commandId.value}},
        {"status", Value{status_name(state.status)}},
        {"reason", Value{reason}}
    }));
}

void RuntimeEffectsState::register_adapter(std::shared_ptr<EffectAdapter> adapter) {
    if (!adapter)
        throw std::invalid_argument("adapter ownership must not be null");
    const auto key = adapter->route().value();
    if (!adapters.emplace(key, adapter).second)
        throw std::invalid_argument("adapter route is already registered");
    const AdapterCapabilities capabilities = adapter->capabilities();
    for (auto& [id, state] : commands) {
        static_cast<void>(id);
        if (state.command.effect.adapterRoute != adapter->route())
            continue;
        state.adapter = adapter;
        state.capabilities = capabilities;
        if (state.status == CommandStatus::Pending &&
            !capabilities.nativeIdempotency) {
            transition(
                state,
                CommandStatus::Indeterminate,
                capabilities.readAfterWriteReconciliation
                    ? "host reconciliation required after restart"
                    : "adapter cannot safely retry after restart"
            );
        }
    }
    store->flush();
}

void RuntimeEffectsState::record_report(
    const EffectReport& report,
    const char* disposition
) {
    append(EventType::ReportReceived, event_payload({
        {"command_id", Value{report.commandId.value}},
        {"session", Value{report.sessionId.value}},
        {"route", Value{report.adapterRoute.value()}},
        {"target", Value{report.target.value()}},
        {"status", Value{status_name(report.status)}},
        {"disposition", Value{disposition}}
    }));
}

void RuntimeEffectsState::validate_effects(std::span<const ResolvedEffect> effects) const {
    std::map<TargetKey, bool> uniqueTargets;
    for (const auto& effect : effects) {
        effect.desiredValue.validate();
        if (adapters.find(effect.adapterRoute.value()) == adapters.end())
            throw std::invalid_argument(
                "resolved effect has no registered adapter route");
        if (!uniqueTargets.emplace(
                target_key(effect.adapterRoute, effect.target), true).second) {
            throw std::invalid_argument(
                "frame contains more than one resolved effect for a target");
        }
    }
}

void RuntimeEffectsState::dispatch(CommandState& state, std::uint64_t now,
              std::vector<EffectReport>& accepted) {
    append(EventType::CommandAttempted, event_payload({
        {"command_id", Value{state.command.commandId.value}},
        {"attempt", Value{static_cast<std::uint64_t>(state.retryIndex + 1)}},
        {"now", Value{now}}
    }));
    store->flush();

    DispatchResult result;
    try {
        result = state.adapter->dispatch(state.command, feedback.sender);
        validate_dispatch_result_for_command(result, state.command);
    } catch (const EventStoreError&) {
        throw;
    } catch (const std::exception& error) {
        if (!state.capabilities.nativeIdempotency)
            transition(state, CommandStatus::Indeterminate, error.what());
        else
            record_status(state, error.what());
        return;
    } catch (...) {
        if (!state.capabilities.nativeIdempotency) {
            transition(state, CommandStatus::Indeterminate,
                "adapter threw an unknown exception");
        } else {
            record_status(state, "adapter threw an unknown exception");
        }
        return;
    }

    if (result.disposition == DispatchDisposition::Rejected) {
        transition(state, CommandStatus::Rejected, result.diagnostic);
        return;
    }
    if (result.disposition == DispatchDisposition::Failed) {
        transition(state, CommandStatus::Failed, result.diagnostic);
        return;
    }
    if (!result.immediateReport)
        return;

    if (timing == FeedbackTiming::Immediate) {
        apply_report(*result.immediateReport, accepted);
        return;
    }

    if (deferredSynchronousReports.size() >= maximumDeferredReports) {
        transition(state, CommandStatus::Indeterminate,
            "synchronous feedback staging capacity exceeded");
        return;
    }
    deferredSynchronousReports.push_back(*result.immediateReport);
}

void RuntimeEffectsState::retry_due(std::uint64_t now, std::vector<EffectReport>& accepted) {
    for (auto& [id, state] : commands) {
        static_cast<void>(id);
        if (state.status != CommandStatus::Pending || !state.adapter)
            continue;

        const auto elapsed = now >= state.command.issuedAtMs
            ? now - state.command.issuedAtMs
            : 0;
        if (elapsed >= static_cast<std::uint64_t>(retry.overallTimeout.count())) {
            transition(state, CommandStatus::TimedOut,
                "overall timeout elapsed");
            continue;
        }

        const auto delay = retry.delay_before_retry(state.retryIndex);
        if (!delay || elapsed < static_cast<std::uint64_t>(delay->count()))
            continue;

        ++state.retryIndex;
        dispatch(state, now, accepted);
    }
}

std::optional<EffectCommand> RuntimeEffectsState::issue(const ResolvedEffect& effect,
                                   std::uint64_t now,
                                   std::vector<EffectReport>& accepted) {
    effect.desiredValue.validate();
    const auto adapter = adapters.find(effect.adapterRoute.value());
    if (adapter == adapters.end())
        throw std::invalid_argument("resolved effect has no registered adapter route");

    const TargetKey key = target_key(effect.adapterRoute, effect.target);
    const auto latest = latestByTarget.find(key);
    if (latest != latestByTarget.end()) {
        CommandState& prior = commands.at(latest->second);
        if (prior.status == CommandStatus::Indeterminate)
            return std::nullopt;
        if (active(prior.status) &&
            prior.command.effect.desiredValue == effect.desiredValue) {
            return std::nullopt;
        }
        if (prior.status == CommandStatus::Pending) {
            transition(prior, CommandStatus::Superseded,
                "newer desired value selected");
        }
    }

    const auto observedState = observed.find(key);
    if (observedState != observed.end() && observedState->second == effect.desiredValue)
        return std::nullopt;

    prune_terminal_history(true);

    if (nextCommand == 0 || nextCommand == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("command sequence exhausted");

    EffectCommand command{session, CommandId{nextCommand++}, effect, now};
    validate_effect_command(command);
    CommandState state{
        command,
        adapter->second,
        adapter->second->capabilities(),
        CommandStatus::Pending,
        0,
        std::nullopt
    };

    append(EventType::CommandIssued, event_payload({
        {"key", Value{"command:" + std::to_string(command.commandId.value)}},
        {"value", Value{"pending"}},
        {"command_id", Value{command.commandId.value}},
        {"route", Value{effect.adapterRoute.value()}},
        {"target", Value{effect.target.value()}},
        {"desired", effect.desiredValue},
        {"issued_at", Value{now}}
    }));

    const auto [inserted, ok] = commands.emplace(command.commandId.value, std::move(state));
    if (!ok)
        throw std::logic_error("duplicate command ID");
    latestByTarget.insert_or_assign(key, command.commandId.value);
    dispatch(inserted->second, now, accepted);
    return command;
}

void RuntimeEffectsState::clear_desire(const AdapterRoute& route, const EffectTarget& target) {
    const TargetKey key = target_key(route, target);
    const auto latest = latestByTarget.find(key);
    if (latest == latestByTarget.end())
        return;
    CommandState& prior = commands.at(latest->second);
    if (prior.status == CommandStatus::Pending) {
        transition(
            prior,
            CommandStatus::Superseded,
            "selected desire disappeared");
    }
}

}
