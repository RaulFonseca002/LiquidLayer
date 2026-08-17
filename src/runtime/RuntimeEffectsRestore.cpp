#include "RuntimeInternals.hpp"

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

}

void RuntimeEffectsState::restore(std::span<const EventRecord> records) {
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

void RuntimeEffectsState::restore_command(
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

void RuntimeEffectsState::restore_observed_record(const Value::Object& object) {
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

void RuntimeEffectsState::restore_observed_checkpoint(const Value::Object& states) {
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

void RuntimeEffectsState::restore_observed_values(const Value::Object& states) {
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

}
