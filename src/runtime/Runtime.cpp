#include "liquid/Runtime.hpp"
#include "liquid/events/MemoryEventStore.hpp"
#include "liquid/events/Replay.hpp"

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <exception>
#include <stdexcept>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace liquid::detail {

namespace {

using TargetKey = std::pair<std::string, std::string>;

TargetKey target_key(const AdapterRoute& route, const EffectTarget& target) {
    return {route.value(), target.value()};
}

const char* status_name(CommandStatus status) {
    switch (status) {
    case CommandStatus::Pending: return "pending";
    case CommandStatus::Applied: return "applied";
    case CommandStatus::Rejected: return "rejected";
    case CommandStatus::Failed: return "failed";
    case CommandStatus::TimedOut: return "timed-out";
    case CommandStatus::Superseded: return "superseded";
    case CommandStatus::Indeterminate: return "indeterminate";
    }
    return "unknown";
}

Value event_payload(std::initializer_list<std::pair<const std::string, Value>> fields) {
    Value::Object object;
    for (const auto& [name, value] : fields)
        object.emplace(name, value);
    return Value{std::move(object)};
}

std::pair<std::string, Value> serialized_topology(
    const TopologyMutation& topology,
    WorldInstanceId runtimeWorld,
    SessionId session
) {
    std::string key = topology.key;
    const std::string world = std::to_string(runtimeWorld);
    const std::string logicalWorld = std::to_string(session.value);
    for (const std::string prefix : {"behavior:", "access:"}) {
        const std::string actualPrefix = prefix + world + ":";
        if (key.starts_with(actualPrefix)) {
            key.replace(prefix.size(), world.size(), logicalWorld);
            break;
        }
    }

    Value value = topology.value;
    if (value.kind() == Value::Kind::Object) {
        Value::Object normalized = value.as_object();
        for (const char* field : {"world", "slot_world", "behavior_world"}) {
            const auto found = normalized.find(field);
            if (found != normalized.end())
                found->second = Value{session.value};
        }
        value = Value{std::move(normalized)};
    }
    return {std::move(key), std::move(value)};
}

bool active(CommandStatus status) {
    return status == CommandStatus::Pending || status == CommandStatus::Indeterminate;
}

const Value& required_field(
    const Value::Object& object,
    const char* name,
    const char* context
) {
    const auto found = object.find(name);
    if (found == object.end())
        throw EventStoreError(std::string(context) + " is missing " + name);
    return found->second;
}

std::uint64_t required_unsigned(
    const Value::Object& object,
    const char* name,
    const char* context
) {
    const Value& value = required_field(object, name, context);
    if (value.kind() != Value::Kind::UnsignedInteger)
        throw EventStoreError(std::string(context) + " field is not unsigned: " + name);
    return value.as_unsigned_integer();
}

std::string required_string(
    const Value::Object& object,
    const char* name,
    const char* context
) {
    const Value& value = required_field(object, name, context);
    if (value.kind() != Value::Kind::String)
        throw EventStoreError(std::string(context) + " field is not a string: " + name);
    return value.as_string();
}

CommandStatus parse_status(const std::string& value) {
    if (value == "pending") return CommandStatus::Pending;
    if (value == "applied") return CommandStatus::Applied;
    if (value == "rejected") return CommandStatus::Rejected;
    if (value == "failed") return CommandStatus::Failed;
    if (value == "timed-out") return CommandStatus::TimedOut;
    if (value == "superseded") return CommandStatus::Superseded;
    if (value == "indeterminate") return CommandStatus::Indeterminate;
    throw EventStoreError("recorded command status is invalid");
}

}

class RuntimeEffectsState {
public:
    struct CommandState {
        EffectCommand command;
        std::shared_ptr<EffectAdapter> adapter;
        AdapterCapabilities capabilities;
        CommandStatus status = CommandStatus::Pending;
        std::size_t retryIndex = 0;
        std::optional<EffectReport> terminalReport;
    };

    SessionId session;
    FeedbackTiming timing;
    RetryPolicy retry;
    FeedbackChannel feedback;
    std::unique_ptr<EventStore> ownedStore;
    EventStore* store = nullptr;
    std::map<std::string, std::shared_ptr<EffectAdapter>> adapters;
    std::map<std::uint64_t, CommandState> commands;
    std::map<TargetKey, std::uint64_t> latestByTarget;
    std::map<TargetKey, std::uint64_t> authoritativeByTarget;
    std::map<TargetKey, Value> observed;
    std::map<TargetKey, StateRevision> observedRevisions;
    std::deque<std::uint64_t> terminalOrder;
    std::deque<EffectReport> deferredSynchronousReports;
    std::size_t maximumRetainedCommands;
    std::size_t maximumDeferredReports;
    std::uint64_t nextCommand = 1;

    explicit RuntimeEffectsState(RuntimeOptions options)
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

    void restore(std::span<const EventRecord> records) {
        std::uint64_t maximumCommandId = 0;
        for (const auto& record : records) {
            if (record.payload.kind() != Value::Kind::Object)
                continue;
            const auto& object = record.payload.as_object();
            if (record.type == EventType::Checkpoint) {
                const auto commandsField = object.find("commands");
                if (commandsField != object.end() &&
                    commandsField->second.kind() == Value::Kind::Object) {
                    for (const auto& [key, value] : commandsField->second.as_object()) {
                        static_cast<void>(key);
                        if (value.kind() != Value::Kind::Object)
                            continue;
                        restore_command(value.as_object(), maximumCommandId);
                    }
                }
                const auto observedField = object.find("observed_state");
                if (observedField != object.end() &&
                    observedField->second.kind() == Value::Kind::Object) {
                    const auto authorityField = object.find("observed_authority");
                    if (authorityField != object.end() &&
                        authorityField->second.kind() == Value::Kind::Object) {
                        restore_observed_checkpoint(
                            authorityField->second.as_object());
                    } else {
                        restore_observed_values(observedField->second.as_object());
                    }
                }
            } else if (record.type == EventType::CommandIssued) {
                restore_command(object, maximumCommandId);
            } else if (record.type == EventType::CommandAttempted) {
                const std::uint64_t id = required_unsigned(
                    object, "command_id", "recorded command attempt");
                const auto found = commands.find(id);
                if (found == commands.end())
                    throw EventStoreError("attempt references unknown command");
                const std::uint64_t attempt = required_unsigned(
                    object, "attempt", "recorded command attempt");
                if (attempt == 0 || attempt > EffectLimits::maxRetryDelays + 1)
                    throw EventStoreError("recorded command attempt is out of range");
                found->second.retryIndex = std::max(
                    found->second.retryIndex,
                    static_cast<std::size_t>(attempt - 1));
            } else if (record.type == EventType::CommandStatusChanged) {
                const std::uint64_t id = required_unsigned(
                    object, "command_id", "recorded command status");
                const auto found = commands.find(id);
                if (found == commands.end())
                    throw EventStoreError("status references unknown command");
                found->second.status = parse_status(required_string(
                    object, "status", "recorded command status"));
            } else if (record.type == EventType::ObservedStateChanged) {
                restore_observed_record(object);
            }
        }
        if (maximumCommandId == std::numeric_limits<std::uint64_t>::max())
            throw EventStoreError("command sequence exhausted");
        nextCommand = maximumCommandId + 1;
        for (const auto& [id, state] : commands) {
            if (!active(state.status))
                terminalOrder.push_back(id);
        }
        prune_terminal_history();
    }

    void restore_command(
        const Value::Object& object,
        std::uint64_t& maximumCommandId
    ) {
                const auto nestedValue = object.find("value");
                const Value::Object* commandObject = &object;
                if (nestedValue != object.end() &&
                    nestedValue->second.kind() == Value::Kind::Object) {
                    commandObject = &nestedValue->second.as_object();
                }
                const std::uint64_t id = required_unsigned(
                    *commandObject, "command_id", "recorded command");
                const auto route = AdapterRoute{
                    required_string(*commandObject, "route", "recorded command")};
                const auto target = EffectTarget{
                    required_string(*commandObject, "target", "recorded command")};
                const Value desired = required_field(
                    *commandObject, "desired", "recorded command");
                const std::uint64_t issuedAt = required_unsigned(
                    *commandObject, "issued_at", "recorded command");
                EffectCommand command{
                    session, CommandId{id},
                    ResolvedEffect{route, target, desired}, issuedAt};
                validate_effect_command(command);
                CommandStatus status = CommandStatus::Pending;
                const auto statusField = commandObject->find("status");
                if (statusField != commandObject->end()) {
                    if (statusField->second.kind() != Value::Kind::String)
                        throw EventStoreError("recorded command status is not a string");
                    status = parse_status(statusField->second.as_string());
                }
                CommandState state{
                    command, nullptr, {}, status, 0, std::nullopt};
                commands.insert_or_assign(id, std::move(state));
                latestByTarget.insert_or_assign(
                    target_key(route, target), id);
                maximumCommandId = std::max(maximumCommandId, id);
    }

    void restore_observed_record(const Value::Object& object) {
        TargetKey key;
        const auto route = object.find("route");
        const auto target = object.find("target");
        if (route != object.end() && target != object.end()) {
            if (route->second.kind() != Value::Kind::String ||
                target->second.kind() != Value::Kind::String)
                throw EventStoreError("recorded observed route and target must be strings");
            key = target_key(
                AdapterRoute{route->second.as_string()},
                EffectTarget{target->second.as_string()});
        } else {
            const std::uint64_t id = required_unsigned(
                object, "command_id", "recorded observed state");
            const auto command = commands.find(id);
            if (command == commands.end())
                throw EventStoreError("observed state references unknown command");
            key = target_key(
                command->second.command.effect.adapterRoute,
                command->second.command.effect.target);
        }

        observed.insert_or_assign(
            key, required_field(object, "observed", "recorded observed state"));
        const auto revision = object.find("state_revision");
        if (revision != object.end()) {
            if (revision->second.kind() != Value::Kind::UnsignedInteger)
                throw EventStoreError("recorded observed revision must be unsigned");
            StateRevision restored{revision->second.as_unsigned_integer()};
            if (!restored.valid())
                throw EventStoreError("recorded observed revision is invalid");
            observedRevisions.insert_or_assign(key, restored);
        }

        const auto idField = object.find("command_id");
        if (idField != object.end()) {
            if (idField->second.kind() != Value::Kind::UnsignedInteger)
                throw EventStoreError("recorded observed command id must be unsigned");
            const std::uint64_t id = idField->second.as_unsigned_integer();
            if (!commands.contains(id))
                throw EventStoreError("observed state references unknown command");
            authoritativeByTarget.insert_or_assign(key, id);
        }
    }

    void restore_observed_checkpoint(const Value::Object& states) {
        for (const auto& [key, value] : states) {
            if (value.kind() != Value::Kind::Object)
                continue;
            const auto& object = value.as_object();
            const auto observedField = object.find("observed");
            if (observedField == object.end())
                continue;
            restore_observed_record(object);
        }
    }

    void restore_observed_values(const Value::Object& states) {
        for (const auto& [key, value] : states) {
            for (const auto& [id, state] : commands) {
                const TargetKey target = target_key(
                    state.command.effect.adapterRoute,
                    state.command.effect.target);
                if (target.first + ":" + target.second != key)
                    continue;
                observed.insert_or_assign(target, value);
                const auto authority = authoritativeByTarget.find(target);
                if (authority == authoritativeByTarget.end() ||
                    authority->second < id) {
                    authoritativeByTarget.insert_or_assign(target, id);
                }
            }
        }
    }

    void prune_terminal_history(bool reserveCommandSlot = false) {
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

    void transition(
        CommandState& state,
        CommandStatus status,
        const std::string& reason = {}
    ) {
        append(EventType::CommandStatusChanged, event_payload({
            {"key", Value{"command:" + std::to_string(state.command.commandId.value)}},
            {"value", Value{status_name(status)}},
            {"command_id", Value{state.command.commandId.value}},
            {"status", Value{status_name(status)}},
            {"reason", Value{reason}}
        }));
        const bool becameTerminal = active(state.status) && !active(status);
        state.status = status;
        if (becameTerminal) {
            terminalOrder.push_back(state.command.commandId.value);
        }
    }

    void append(EventType type, Value payload,
                Durability durability = Durability::Buffered) {
        EventData data;
        data.type = type;
        data.payload = std::move(payload);
        store->append(std::move(data), durability);
    }

    void record_status(const CommandState& state, const std::string& reason = {}) {
        append(EventType::CommandStatusChanged, event_payload({
            {"key", Value{"command:" + std::to_string(state.command.commandId.value)}},
            {"value", Value{status_name(state.status)}},
            {"command_id", Value{state.command.commandId.value}},
            {"status", Value{status_name(state.status)}},
            {"reason", Value{reason}}
        }));
    }

    void register_adapter(std::shared_ptr<EffectAdapter> adapter) {
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

    void record_report(
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

    void validate_effects(std::span<const ResolvedEffect> effects) const {
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

    void apply_report(const EffectReport& report, std::vector<EffectReport>& accepted) {
        validate_effect_report(report);

        const auto found = commands.find(report.commandId.value);
        if (report.sessionId != session || found == commands.end()) {
            record_report(report, "rejected-unknown-or-cross-session");
            return;
        }

        CommandState& state = found->second;
        if (report.adapterRoute != state.command.effect.adapterRoute ||
            report.target != state.command.effect.target) {
            record_report(report, "rejected-mismatched-target");
            return;
        }

        if (state.terminalReport) {
            if (*state.terminalReport == report) {
                record_report(report, "duplicate");
                return;
            }
            record_report(report, "rejected-conflicting-duplicate");
            return;
        }

        if (state.status == CommandStatus::Superseded &&
            report.status != CommandStatus::Applied) {
            record_report(report, "rejected-stale-terminal");
            return;
        }

        record_report(report, "accepted");
        const bool wasSuperseded = state.status == CommandStatus::Superseded;
        if (!wasSuperseded)
            transition(state, report.status, report.diagnostic);
        state.terminalReport = report;
        accepted.push_back(report);

        if (report.status != CommandStatus::Applied)
            return;

        const TargetKey key = target_key(report.adapterRoute, report.target);
        const auto revision = observedRevisions.find(key);
        if (revision != observedRevisions.end() &&
            revision->second > report.stateRevision) {
            return;
        }
        if (revision != observedRevisions.end() &&
            revision->second == report.stateRevision) {
            const auto prior = observed.find(key);
            if (prior != observed.end() &&
                prior->second == *report.observedValue) {
                return;
            }
            record_report(report, "rejected-conflicting-state-revision");
            return;
        }

        append(EventType::ObservedStateChanged, event_payload({
            {"key", Value{report.adapterRoute.value() + ":" + report.target.value()}},
            {"value", *report.observedValue},
            {"command_id", Value{report.commandId.value}},
            {"route", Value{report.adapterRoute.value()}},
            {"target", Value{report.target.value()}},
            {"state_revision", Value{report.stateRevision.value}},
            {"observed", *report.observedValue}
        }));
        observed.insert_or_assign(key, *report.observedValue);
        observedRevisions.insert_or_assign(key, report.stateRevision);
        authoritativeByTarget.insert_or_assign(key, report.commandId.value);
    }

    void apply_observation(
        const ExternalObservation& observation,
        std::vector<ExternalObservation>& accepted
    ) {
        validate_external_observation(observation);
        const TargetKey key = target_key(
            observation.adapterRoute, observation.target);
        std::string disposition = "accepted";
        if (observation.sessionId != session ||
            adapters.find(observation.adapterRoute.value()) == adapters.end()) {
            disposition = "rejected-unknown-or-cross-session";
        } else {
            const auto revision = observedRevisions.find(key);
            if (revision != observedRevisions.end() &&
                revision->second > observation.stateRevision) {
                disposition = "stale";
            } else if (revision != observedRevisions.end() &&
                revision->second == observation.stateRevision) {
                const auto prior = observed.find(key);
                disposition = prior != observed.end() &&
                    prior->second == observation.observedValue
                    ? "duplicate" : "rejected-conflicting-revision";
            }
        }

        append(EventType::ExternalObservationReceived, event_payload({
            {"session", Value{observation.sessionId.value}},
            {"route", Value{observation.adapterRoute.value()}},
            {"target", Value{observation.target.value()}},
            {"state_revision", Value{observation.stateRevision.value}},
            {"observed_at", Value{observation.observedAtMs}},
            {"observed", observation.observedValue},
            {"disposition", Value{disposition}}
        }));
        if (disposition != "accepted")
            return;

        append(EventType::ObservedStateChanged, event_payload({
            {"key", Value{observation.adapterRoute.value() + ":" +
                observation.target.value()}},
            {"value", observation.observedValue},
            {"route", Value{observation.adapterRoute.value()}},
            {"target", Value{observation.target.value()}},
            {"state_revision", Value{observation.stateRevision.value}},
            {"observed", observation.observedValue},
            {"source", Value{"external"}}
        }));
        observed.insert_or_assign(key, observation.observedValue);
        observedRevisions.insert_or_assign(key, observation.stateRevision);
        accepted.push_back(observation);
    }

    void process_feedback(
        std::vector<EffectReport>& accepted,
        std::vector<ExternalObservation>& observations
    ) {
        while (!deferredSynchronousReports.empty()) {
            apply_report(deferredSynchronousReports.front(), accepted);
            deferredSynchronousReports.pop_front();
        }
        while (auto report = feedback.receiver.try_receive()) {
            try {
                apply_report(*report, accepted);
            } catch (...) {
                deferredSynchronousReports.push_front(std::move(*report));
                throw;
            }
        }
        while (auto observation = feedback.receiver.try_receive_observation())
            apply_observation(*observation, observations);
    }

    bool report_is_authoritative(const EffectReport& report) const {
        if (report.status != CommandStatus::Applied)
            return false;
        const TargetKey key = target_key(
            report.adapterRoute, report.target);
        const auto authority = authoritativeByTarget.find(key);
        const auto revision = observedRevisions.find(key);
        const auto value = observed.find(key);
        return authority != authoritativeByTarget.end() &&
            authority->second == report.commandId.value &&
            revision != observedRevisions.end() &&
            revision->second == report.stateRevision &&
            value != observed.end() && value->second == *report.observedValue;
    }

    bool observation_is_authoritative(
        const ExternalObservation& observation
    ) const {
        const TargetKey key = target_key(
            observation.adapterRoute, observation.target);
        const auto revision = observedRevisions.find(key);
        const auto value = observed.find(key);
        return revision != observedRevisions.end() &&
            revision->second == observation.stateRevision &&
            value != observed.end() &&
            value->second == observation.observedValue;
    }

    void dispatch(CommandState& state, std::uint64_t now,
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

    void retry_due(std::uint64_t now, std::vector<EffectReport>& accepted) {
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

    std::optional<EffectCommand> issue(const ResolvedEffect& effect,
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

    void clear_desire(const AdapterRoute& route, const EffectTarget& target) {
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
};

}

namespace liquid {

Runtime::Runtime() = default;

Runtime::Runtime(FrameNumber initialFrame)
    : currentFrame(initialFrame) {
}

Runtime::Runtime(liquid::RuntimeOptions options)
    : effectsState(
          std::make_unique<liquid::detail::RuntimeEffectsState>(std::move(options))) {
    ownedWorld.enable_script_evidence();
}

Runtime::~Runtime() = default;

void Runtime::ensure_owner_thread() const {
    if (std::this_thread::get_id() != ownerThread)
        throw std::logic_error("runtime used from a non-owner thread");
}

World& Runtime::world() {
    ensure_owner_thread();
    return ownedWorld;
}

const World& Runtime::world() const {
    ensure_owner_thread();
    return ownedWorld;
}

FrameNumber Runtime::frame() const {
    ensure_owner_thread();
    return currentFrame;
}

bool Runtime::faulted() const {
    ensure_owner_thread();
    return faultedState;
}

FrameLog Runtime::run_frame(
    IntentTime now,
    std::map<ComponentTypeId, std::map<ComponentName, ComponentSlotId>> resolutions
) {
    ensure_owner_thread();
    validate_frame_start(now, resolutions);
    frameInProgress = true;

    try {
        latestFrameLog = execute_frame_phases(now, resolutions);
    } catch (...) {
        frameInProgress = false;
        faultedState = true;
        throw;
    }

    frameInProgress = false;
    ++currentFrame;
    return latestFrameLog;
}

void Runtime::validate_frame_start(
    IntentTime now,
    const std::map<ComponentTypeId,
        std::map<ComponentName, ComponentSlotId>>& resolutions
) const {
    if (currentFrame == std::numeric_limits<FrameNumber>::max())
        throw std::overflow_error("frame number exhausted");
    if (faultedState)
        throw std::logic_error("runtime is faulted after an incomplete frame");

    if (frameInProgress)
        throw std::logic_error("runtime frame execution is not reentrant");

    if (currentFrame > 0 && now < latestFrameLog.now)
        throw std::invalid_argument("runtime time cannot move backwards");

    for (const auto& [type, components] : resolutions) {
        if (!ownedWorld.resolution_request_is_current(type, components))
            throw std::invalid_argument("intent resolution request contains a stale component target");
    }
}

FrameLog Runtime::execute_frame_phases(
    IntentTime now,
    const std::map<ComponentTypeId,
        std::map<ComponentName, ComponentSlotId>>& resolutions
) {
    FrameLog log;
    log.frame = currentFrame;
    log.now = now;
    std::size_t completedInPhase = 0;

    try {
        log.phases.push_back("begin_frame");

        log.phases.push_back("expire_intents");
        log.expired_intents = ownedWorld.destroy_expired_intents(now);

        log.phases.push_back("run_input_systems");
        ownedWorld.run_systems(
            currentFrame, now, SystemPhase::Input,
            &completedInPhase, &log.failure_system);
        log.systems_run += completedInPhase;
        completedInPhase = 0;

        log.phases.push_back("run_behavior_systems");
        ownedWorld.run_systems(
            currentFrame, now, SystemPhase::Behavior,
            &completedInPhase, &log.failure_system);
        log.systems_run += completedInPhase;
        completedInPhase = 0;

        log.phases.push_back("run_decision_systems");
        ownedWorld.run_systems(
            currentFrame, now, SystemPhase::Decision,
            &completedInPhase, &log.failure_system);
        log.systems_run += completedInPhase;
        completedInPhase = 0;

        log.phases.push_back("resolve_intents");
        log.expired_intents += ownedWorld.destroy_expired_intents(now);
        auto resolvedTargets = ownedWorld.resolution_targets();
        for (const auto& [type, components] : resolutions) {
            auto& targets = resolvedTargets[type];
            for (const auto& [name, slot] : components)
                targets.insert_or_assign(name, slot);
        }
        log.resolution_requests = resolvedTargets.size();

        for (const auto& [type, components] : resolvedTargets) {
            std::map<ComponentName, IntentId> selected = ownedWorld.resolve_intents(type, components, now);
            log.selected_intents += selected.size();
            log.intent_selections.emplace(type, std::move(selected));
        }

        log.phases.push_back("end_frame");
        log.completed = true;
    } catch (const std::exception& error) {
        log.systems_run += completedInPhase;
        log.failure_phase = log.phases.empty() ? "begin_frame" : log.phases.back();
        log.failure_message = error.what();
        latestFrameLog = log;
        throw;
    } catch (...) {
        log.systems_run += completedInPhase;
        log.failure_phase = log.phases.empty() ? "begin_frame" : log.phases.back();
        log.failure_message = "unknown exception";
        latestFrameLog = log;
        throw;
    }
    return log;
}

void Runtime::record_world_evidence() {
    for (const auto& topology : ownedWorld.topology_mutations()) {
        auto [key, value] = liquid::detail::serialized_topology(
            topology, ownedWorld.instance_id(), effectsState->session);
        effectsState->append(liquid::EventType::TopologyChanged,
            liquid::detail::event_payload({
                {"key", liquid::Value{std::move(key)}},
                {"value", std::move(value)},
                {"removed", liquid::Value{topology.removed}}
            }));
    }
    ownedWorld.clear_topology_mutations();
    for (const auto& mutation : ownedWorld.component_mutations()) {
        effectsState->append(
            mutation.removed
                ? liquid::EventType::ComponentRemoved
                : liquid::EventType::ComponentMutated,
            liquid::detail::event_payload({
                {"key", liquid::Value{
                    std::to_string(mutation.target.type) + ":" + mutation.name}},
                {"value", mutation.after},
                {"type", liquid::Value{
                    static_cast<std::uint64_t>(mutation.target.type)}},
                {"slot_world", liquid::Value{effectsState->session.value}},
                {"slot", liquid::Value{
                    static_cast<std::uint64_t>(mutation.target.slot.slot)}},
                {"generation", liquid::Value{
                    static_cast<std::uint64_t>(mutation.target.slot.generation)}},
                {"name", liquid::Value{mutation.name}},
                {"before", mutation.before},
                {"after", mutation.after}
            }));
    }
    ownedWorld.clear_component_mutations();

    for (const auto& lifecycle : ownedWorld.intent_lifecycle_records()) {
        const auto& intent = lifecycle.intent;
        const std::string key = "intent:" +
            std::to_string(effectsState->session.value) + ":" +
            std::to_string(intent.id.slot) + ":" +
            std::to_string(intent.id.generation);
        if (!lifecycle.created) {
            effectsState->append(liquid::EventType::IntentDestroyed,
                liquid::detail::event_payload({
                    {"key", liquid::Value{key}},
                    {"intent_world", liquid::Value{effectsState->session.value}},
                    {"intent_slot", liquid::Value{
                        static_cast<std::uint64_t>(intent.id.slot)}},
                    {"intent_generation", liquid::Value{
                        static_cast<std::uint64_t>(intent.id.generation)}}
                }));
            continue;
        }
        effectsState->append(liquid::EventType::IntentCreated,
            liquid::detail::event_payload({
                {"key", liquid::Value{key}},
                {"value", intent.encodedValue},
                {"intent_world", liquid::Value{effectsState->session.value}},
                {"intent_slot", liquid::Value{
                    static_cast<std::uint64_t>(intent.id.slot)}},
                {"intent_generation", liquid::Value{
                    static_cast<std::uint64_t>(intent.id.generation)}},
                {"owner_world", liquid::Value{effectsState->session.value}},
                {"owner_slot", liquid::Value{
                    static_cast<std::uint64_t>(intent.owner.slot)}},
                {"owner_generation", liquid::Value{
                    static_cast<std::uint64_t>(intent.owner.generation)}},
                {"type", liquid::Value{
                    static_cast<std::uint64_t>(intent.target.type)}},
                {"target_world", liquid::Value{effectsState->session.value}},
                {"target_slot", liquid::Value{
                    static_cast<std::uint64_t>(intent.target.slot.slot)}},
                {"target_generation", liquid::Value{
                    static_cast<std::uint64_t>(intent.target.slot.generation)}},
                {"priority", liquid::Value{
                    static_cast<std::uint64_t>(intent.priority)}},
                {"name", liquid::Value{intent.name}},
                {"sequence", liquid::Value{intent.sequence}},
                {"lifetime", liquid::Value{
                    intent.lifetime.kind == IntentLifetimeKind::Persistent
                        ? "persistent" : "until-time"}},
                {"expires_at", liquid::Value{intent.lifetime.expiresAt}}
            }));
    }
    ownedWorld.clear_intent_lifecycle_records();

    // GCOVR_EXCL_START: script evidence is produced only through the Lua
    // boundary, which the Core-only coverage gate excludes by design; the
    // Lua-enabled suite (test_lua_lifecycle, test_lua_behavior) covers it.
    for (const auto& script : ownedWorld.script_execution_evidence()) {
        liquid::Value::Object payload;
        payload.emplace("key", liquid::Value{
            "script:" + std::to_string(effectsState->session.value) + ":" +
            std::to_string(script.owner.slot) + ":" +
            std::to_string(script.owner.generation) + ":" +
            std::to_string(script.now)});
        payload.emplace("owner_world", liquid::Value{effectsState->session.value});
        payload.emplace("owner_slot", liquid::Value{
            static_cast<std::uint64_t>(script.owner.slot)});
        payload.emplace("owner_generation", liquid::Value{
            static_cast<std::uint64_t>(script.owner.generation)});
        payload.emplace("now", liquid::Value{script.now});
        payload.emplace("source_included", liquid::Value{script.sourceIncluded});
        payload.emplace("source_hash", liquid::Value{script.sourceHash});
        if (script.sourceIncluded)
            payload.emplace("source", liquid::Value{script.source});
        payload.emplace("status", liquid::Value{
            static_cast<std::uint64_t>(script.status)});
        payload.emplace("diagnostic", liquid::Value{script.diagnostic});
        payload.emplace("created_intents", liquid::Value{
            static_cast<std::uint64_t>(script.createdIntentCount)});
        effectsState->append(
            liquid::EventType::ScriptExecuted,
            liquid::Value{std::move(payload)});
    }
    // GCOVR_EXCL_STOP
    ownedWorld.clear_script_execution_evidence();
}

std::vector<ResolvedEffect> Runtime::apply_selections(
    const FrameLog& frame
) {
    std::vector<ResolvedEffect> effects;
    std::set<ComponentTarget> selectedExternalComponents;
    for (const auto& [type, selections] : frame.intent_selections) {
        for (const auto& [name, id] : selections) {
            const Intent& selected = ownedWorld.intent(id);
            const ComponentTarget target = selected.target;
            const auto configured = componentControls.find(target);
            const ComponentControl control = configured == componentControls.end()
                ? ComponentControl::SelectionOnly
                : configured->second;

            if (control == ComponentControl::SelectionOnly)
                continue;
            if (control == ComponentControl::InternalState) {
                ownedWorld.replace_component_value(
                    target, name, selected.encodedValue);
                continue;
            }

            const auto binding = effectBindings.find(target);
            if (binding == effectBindings.end())
                throw std::logic_error(
                    "external component selection has no effect binding");
            auto effect = ownedWorld.encode_effect(
                target, name, selected.encodedValue);
            if (!effect)
                continue;
            if (effect->adapterRoute != binding->second.route ||
                effect->target != binding->second.target) {
                throw std::invalid_argument(
                    "effect codec output does not match the stable binding");
            }
            selectedExternalComponents.insert(target);
            effects.push_back(std::move(*effect));
        }
    }
    for (const auto& [target, binding] : effectBindings) {
        if (!selectedExternalComponents.contains(target))
            effectsState->clear_desire(binding.route, binding.target);
    }
    return effects;
}

void Runtime::project_authoritative_reports(
    const std::vector<EffectReport>& reports
) {
    for (const EffectReport& report : reports) {
        if (!effectsState->report_is_authoritative(report))
            continue;
        const auto bound = componentsByEffectTarget.find({
            report.adapterRoute.value(), report.target.value()});
        if (bound == componentsByEffectTarget.end())
            continue;
        const auto binding = effectBindings.find(bound->second);
        if (binding == effectBindings.end())
            throw std::logic_error("effect target binding is inconsistent");
        Value projected = ownedWorld.decode_observed(
            binding->second.component, *report.observedValue);
        ownedWorld.replace_component_value(
            binding->second.component,
            binding->second.name,
            projected);
    }
}

void Runtime::project_authoritative_observations(
    const std::vector<ExternalObservation>& observations
) {
    for (const ExternalObservation& observation : observations) {
        if (!effectsState->observation_is_authoritative(observation))
            continue;
        const auto bound = componentsByEffectTarget.find({
            observation.adapterRoute.value(), observation.target.value()});
        if (bound == componentsByEffectTarget.end())
            continue;
        const auto binding = effectBindings.find(bound->second);
        if (binding == effectBindings.end())
            throw std::logic_error("effect target binding is inconsistent");
        Value projected = ownedWorld.decode_observed(
            binding->second.component, observation.observedValue);
        ownedWorld.replace_component_value(
            binding->second.component,
            binding->second.name,
            projected);
    }
}

liquid::FrameResult Runtime::run_frame(liquid::FrameInput input) {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    validate_frame_start(input.now, input.resolutions);
    effectsState->validate_effects(input.resolvedEffects);
    frameInProgress = true;

    liquid::FrameResult result;
    const FrameNumber frameBeingRun = currentFrame;
    bool worldEvidenceAttempted = false;
    try {
        effectsState->append(liquid::EventType::FrameStarted,
            liquid::detail::event_payload({
                {"frame", liquid::Value{currentFrame}},
                {"now", liquid::Value{input.now}}
            }));
        effectsState->process_feedback(
            result.reports, result.observations);
        project_authoritative_reports(result.reports);
        project_authoritative_observations(result.observations);
        result.frame = execute_frame_phases(input.now, input.resolutions);
        std::vector<ResolvedEffect> resolvedEffects =
            apply_selections(result.frame);
        resolvedEffects.insert(
            resolvedEffects.end(),
            input.resolvedEffects.begin(), input.resolvedEffects.end());
        effectsState->validate_effects(resolvedEffects);
        for (const auto& [type, selections] : result.frame.intent_selections) {
            for (const auto& [name, id] : selections) {
                effectsState->append(liquid::EventType::ResolutionSelected,
                    liquid::detail::event_payload({
                        {"key", liquid::Value{
                            std::to_string(type) + ":" + name}},
                        {"value", liquid::Value{effectsState->session.value}},
                        {"type", liquid::Value{static_cast<std::uint64_t>(type)}},
                        {"name", liquid::Value{name}},
                        {"intent_world", liquid::Value{effectsState->session.value}},
                        {"intent_slot", liquid::Value{
                            static_cast<std::uint64_t>(id.slot)}},
                        {"intent_generation", liquid::Value{
                            static_cast<std::uint64_t>(id.generation)}}
                    }));
            }
        }
        for (const auto& effect : resolvedEffects) {
            auto command = effectsState->issue(effect, input.now, result.reports);
            if (command)
                result.commands.push_back(std::move(*command));
        }
        effectsState->retry_due(input.now, result.reports);
        project_authoritative_reports(result.reports);
        worldEvidenceAttempted = true;
        record_world_evidence();
        effectsState->prune_terminal_history();
        effectsState->append(liquid::EventType::FrameCompleted,
            liquid::detail::event_payload({
                {"frame", liquid::Value{result.frame.frame}},
                {"now", liquid::Value{input.now}}
            }));
        effectsState->store->flush();
    } catch (...) {
        const std::exception_ptr failure = std::current_exception();
        faultedState = true;
        if (!result.frame.completed && !latestFrameLog.completed &&
            latestFrameLog.frame == frameBeingRun) {
            result.frame = latestFrameLog;
        }
        result.frame.completed = false;
        if (result.frame.failure_phase.empty()) {
            result.frame.frame = frameBeingRun;
            result.frame.now = input.now;
            result.frame.failure_phase = "effects_and_persistence";
            try {
                std::rethrow_exception(failure);
            } catch (const std::exception& error) {
                result.frame.failure_message = error.what();
            } catch (...) {
                result.frame.failure_message = "unknown failure";
            }
        }
        try {
            if (!worldEvidenceAttempted)
                record_world_evidence();
            effectsState->append(liquid::EventType::FrameFailed,
                liquid::detail::event_payload({
                    {"frame", liquid::Value{frameBeingRun}},
                    {"now", liquid::Value{input.now}},
                    {"phase", liquid::Value{result.frame.failure_phase}},
                    {"message", liquid::Value{result.frame.failure_message}},
                    {"system", liquid::Value{result.frame.failure_system}},
                    {"expired_intents", liquid::Value{
                        static_cast<std::uint64_t>(result.frame.expired_intents)}},
                    {"selected_intents", liquid::Value{
                        static_cast<std::uint64_t>(result.frame.selected_intents)}},
                    {"systems_run", liquid::Value{
                        static_cast<std::uint64_t>(result.frame.systems_run)}}
                }));
            effectsState->store->flush();
        } catch (...) {
        }
        frameInProgress = false;
        throw;
    }
    latestFrameLog = result.frame;
    frameInProgress = false;
    ++currentFrame;
    return result;
}

void Runtime::register_adapter(std::shared_ptr<liquid::EffectAdapter> adapter) {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    effectsState->register_adapter(std::move(adapter));
}

liquid::FeedbackSender Runtime::feedback_sender() const {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    return effectsState->feedback.sender;
}

std::optional<liquid::Value> Runtime::observed_state(
    const liquid::AdapterRoute& route,
    const liquid::EffectTarget& target
) const {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    const auto found = effectsState->observed.find(
        liquid::detail::target_key(route, target));
    if (found == effectsState->observed.end())
        return std::nullopt;
    return found->second;
}

std::optional<liquid::CommandStatus> Runtime::command_status(
    liquid::CommandId commandId
) const {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    const auto found = effectsState->commands.find(commandId.value);
    if (found == effectsState->commands.end())
        return std::nullopt;
    return found->second.status;
}

void Runtime::reconcile_indeterminate(
    const liquid::AdapterRoute& route,
    const liquid::EffectTarget& target,
    liquid::Value observedValue
) {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    observedValue.validate();
    const auto key = liquid::detail::target_key(route, target);
    const auto latest = effectsState->latestByTarget.find(key);
    if (latest == effectsState->latestByTarget.end() ||
        effectsState->commands.at(latest->second).status !=
            liquid::CommandStatus::Indeterminate) {
        throw std::logic_error("target has no indeterminate command to reconcile");
    }

    auto& state = effectsState->commands.at(latest->second);
    const liquid::CommandStatus reconciledStatus =
        state.command.effect.desiredValue == observedValue
        ? liquid::CommandStatus::Applied
        : liquid::CommandStatus::Failed;
    const liquid::Value reconciledObserved = observedValue;
    std::uint64_t revisionValue = state.command.commandId.value;
    const auto priorRevision = effectsState->observedRevisions.find(key);
    if (priorRevision != effectsState->observedRevisions.end()) {
        if (priorRevision->second.value == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("state revision exhausted");
        revisionValue = priorRevision->second.value + 1;
    }
    const liquid::StateRevision revision{revisionValue};
    liquid::EffectReport terminalReport{
        effectsState->session,
        state.command.commandId,
        route,
        target,
        reconciledStatus,
        reconciledStatus == liquid::CommandStatus::Applied
            ? std::optional<liquid::Value>{observedValue}
            : std::nullopt,
        "host reconciliation",
        state.command.issuedAtMs,
        revision
    };
    effectsState->transition(state, reconciledStatus, "host reconciliation");
    effectsState->append(liquid::EventType::ObservedStateChanged,
        liquid::detail::event_payload({
            {"key", liquid::Value{route.value() + ":" + target.value()}},
            {"value", observedValue},
            {"command_id", liquid::Value{state.command.commandId.value}},
            {"route", liquid::Value{route.value()}},
            {"target", liquid::Value{target.value()}},
            {"state_revision", liquid::Value{revision.value}},
            {"observed", std::move(observedValue)}
        }));
    state.terminalReport = std::move(terminalReport);
    effectsState->observed.insert_or_assign(key, reconciledObserved);
    effectsState->observedRevisions.insert_or_assign(key, revision);
    effectsState->authoritativeByTarget.insert_or_assign(
        key, state.command.commandId.value);
    effectsState->store->flush();
}

RecordId Runtime::checkpoint() {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error("runtime effects were not configured");
    if (frameInProgress)
        throw std::logic_error("runtime checkpoint is unavailable during a frame");
    record_world_evidence();
    ReplayProjector projector;
    const auto records = effectsState->store->read_all();
    const SerializedWorldState state = projector.project(
        effectsState->store->metadata(), records);
    const RecordId checkpointId = effectsState->store->checkpoint(
        projector.checkpoint_payload(state), Durability::Durable);
    effectsState->store->flush();
    return checkpointId;
}

const FrameLog& Runtime::last_frame_log() const {
    ensure_owner_thread();
    return latestFrameLog;
}

}
