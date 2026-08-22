#include "RuntimeInternals.hpp"
#include "../events/EventInternals.hpp"
#include "liquid/events/Replay.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace liquid::detail {

namespace {

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

const Value::Object& required_object_payload(
    const EventRecord& record,
    const char* context
) {
    if (record.payload.kind() != Value::Kind::Object)
        throw EventStoreError(std::string(context) + " payload must be an object");
    return record.payload.as_object();
}

const Value::Object& recorded_command_object(const Value::Object& object) {
    const auto nestedValue = object.find("value");
    if (nestedValue != object.end() &&
        nestedValue->second.kind() == Value::Kind::Object) {
        return nestedValue->second.as_object();
    }
    return object;
}

}

void RuntimeEffectsState::restore(std::span<const EventRecord> records) {
    ReplayProjector{}.project(store->metadata(), records);

    std::uint64_t maximumCommandId = 0;
    for (const auto& record : records) {
        if (record.type == EventType::Checkpoint) {
            const auto& object = required_object_payload(record, "checkpoint");
            events_detail::validate_checkpoint_anchor(
                record.payload, session, record.sequence);
            commands.clear();
            latestByTarget.clear();
            authoritativeByTarget.clear();
            observed.clear();
            observedRevisions.clear();
            terminalOrder.clear();
            maximumCommandId = 0;

            for (const auto& [key, value] :
                events_detail::checkpoint_object(object, "commands")) {
                restore_command(value.as_object(), maximumCommandId);
                const Value::Object& command = recorded_command_object(
                    value.as_object());
                const std::uint64_t id = required_unsigned(
                    command, "command_id", "checkpoint command");
                if (key != "command:" + std::to_string(id))
                    throw EventStoreError("checkpoint command key does not match command ID");
            }
            for (const Value& encoded :
                 events_detail::checkpoint_array(object, "command_attempts")) {
                const Value::Object& attemptRecord = encoded.as_object();
                if (required_unsigned(
                        attemptRecord, "type", "checkpoint command attempt") !=
                    static_cast<std::uint64_t>(EventType::CommandAttempted)) {
                    throw EventStoreError(
                        "checkpoint command_attempts contains another event type");
                }
                const Value& payload = required_field(
                    attemptRecord, "payload", "checkpoint command attempt");
                if (payload.kind() != Value::Kind::Object)
                    throw EventStoreError(
                        "checkpoint command attempt payload must be an object");
                restore_attempt(payload.as_object());
            }
            restore_observed_checkpoint(
                events_detail::checkpoint_object(object, "observed_authority"),
                events_detail::checkpoint_object(object, "observed_state"));
        } else if (record.type == EventType::CommandIssued) {
            const auto& object = required_object_payload(record, "recorded command");
            restore_command(object, maximumCommandId);
        } else if (record.type == EventType::CommandAttempted) {
            restore_attempt(required_object_payload(
                record, "recorded command attempt"));
        } else if (record.type == EventType::CommandStatusChanged) {
            const auto& object = required_object_payload(
                record, "recorded command status");
            const std::uint64_t id = required_unsigned(
                object, "command_id", "recorded command status");
            const auto found = commands.find(id);
            if (found == commands.end())
                throw EventStoreError("status references unknown command");
            // Raw write, deliberately not set_status(): restore rebuilds
            // terminalOrder once below instead of per record. Any new
            // terminal side effect added to set_status() in
            // RuntimeEffectsState.cpp must be mirrored in that rebuild.
            found->second.status = parse_status(required_string(
                object, "status", "recorded command status"));
        } else if (record.type == EventType::ObservedStateChanged) {
            const auto& object = required_object_payload(
                record, "recorded observed state");
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

void RuntimeEffectsState::restore_attempt(const Value::Object& object) {
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
}

void RuntimeEffectsState::restore_command(
    const Value::Object& object,
    std::uint64_t& maximumCommandId
) {
    const Value::Object* commandObject = &recorded_command_object(object);
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
    const TargetKey key = target_key(route, target);
    const auto latest = latestByTarget.find(key);
    if (latest == latestByTarget.end() || latest->second < id)
        latestByTarget.insert_or_assign(key, id);
    maximumCommandId = std::max(maximumCommandId, id);
}

void RuntimeEffectsState::restore_observed_record(const Value::Object& object) {
    const AdapterRoute route{
        required_string(object, "route", "recorded observed state")};
    const EffectTarget target{
        required_string(object, "target", "recorded observed state")};
    const TargetKey key = target_key(route, target);

    observed.insert_or_assign(
        key, required_field(object, "observed", "recorded observed state"));
    StateRevision restored{required_unsigned(
        object, "state_revision", "recorded observed state")};
    if (!restored.valid())
        throw EventStoreError("recorded observed revision is invalid");
    observedRevisions.insert_or_assign(key, restored);

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

void RuntimeEffectsState::restore_observed_checkpoint(
    const Value::Object& authority,
    const Value::Object& values
) {
    for (const auto& [key, value] : authority) {
        const auto& object = value.as_object();
        const std::string route = required_string(
            object, "route", "checkpoint observed authority");
        const std::string target = required_string(
            object, "target", "checkpoint observed authority");
        if (key != route + ":" + target)
            throw EventStoreError("checkpoint observed-authority key is inconsistent");
        const auto observedValue = values.find(key);
        if (observedValue == values.end() ||
            observedValue->second != required_field(
                object, "observed", "checkpoint observed authority")) {
            throw EventStoreError(
                "checkpoint observed state does not match its authority record");
        }
        restore_observed_record(object);
    }
    if (observed.size() != values.size()) {
        throw EventStoreError(
            "checkpoint observed state is missing an authority record");
    }
}

}
