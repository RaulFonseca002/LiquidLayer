#include "liquid/events/Replay.hpp"

#include "liquid/events/EventStore.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace liquid {
namespace {

const Value::Object& require_object(const Value& value, const char* context) {
    if (value.kind() != Value::Kind::Object)
        throw EventStoreError(std::string(context) + " payload must be an object");

    return value.as_object();
}

const Value& require_field(const Value::Object& object,
                           const char* name,
                           const char* context) {
    const auto field = object.find(name);
    if (field == object.end())
        throw EventStoreError(std::string(context) + " payload is missing " + name);

    return field->second;
}

std::string require_key(const Value::Object& object, const char* context) {
    const Value& key = require_field(object, "key", context);
    if (key.kind() != Value::Kind::String)
        throw EventStoreError(std::string(context) + " key must be a string");
    return key.as_string();
}

void set_entry(Value::Object& target,
               const EventRecord& record,
               const char* context) {
    const Value::Object& payload = require_object(record.payload, context);
    target.insert_or_assign(require_key(payload, context),
                            require_field(payload, "value", context));
}

void erase_entry(Value::Object& target,
                 const EventRecord& record,
                 const char* context) {
    const Value::Object& payload = require_object(record.payload, context);
    target.erase(require_key(payload, context));
}

Value::Object optional_object_field(const Value::Object& checkpoint,
                                    const char* name) {
    const auto field = checkpoint.find(name);
    if (field == checkpoint.end())
        throw EventStoreError(std::string("checkpoint is missing field: ") + name);

    if (field->second.kind() != Value::Kind::Object)
        throw EventStoreError(std::string("checkpoint field must be an object: ") + name);

    return field->second.as_object();
}

Value::Object object_field_or_empty(const Value::Object& checkpoint,
                                    const char* name) {
    const auto field = checkpoint.find(name);
    if (field == checkpoint.end())
        return {};
    if (field->second.kind() != Value::Kind::Object)
        throw EventStoreError(std::string("checkpoint field must be an object: ") + name);
    return field->second.as_object();
}

Value record_value(const EventRecord& record) {
    Value::Object encoded;
    encoded.emplace("sequence", Value(record.sequence.value));
    encoded.emplace("type", Value(static_cast<std::uint64_t>(record.type)));
    encoded.emplace("version", Value(static_cast<std::uint64_t>(record.version)));
    encoded.emplace("payload", record.payload);
    return Value(std::move(encoded));
}

Value records_value(const std::vector<EventRecord>& records) {
    Value::Array encoded;
    encoded.reserve(records.size());
    for (const EventRecord& record : records)
        encoded.push_back(record_value(record));
    return Value(std::move(encoded));
}

std::uint64_t require_unsigned(const Value::Object& object,
                               const char* name,
                               const char* context) {
    const Value& value = require_field(object, name, context);
    if (value.kind() != Value::Kind::UnsignedInteger)
        throw EventStoreError(std::string(context) + " field must be unsigned: " + name);
    return value.as_unsigned_integer();
}

std::vector<EventRecord> records_field_or_empty(const Value::Object& checkpoint,
                                                const char* name) {
    const auto field = checkpoint.find(name);
    if (field == checkpoint.end())
        return {};
    if (field->second.kind() != Value::Kind::Array)
        throw EventStoreError(std::string("checkpoint field must be an array: ") + name);

    std::vector<EventRecord> records;
    records.reserve(field->second.as_array().size());
    for (const Value& encoded : field->second.as_array()) {
        const Value::Object& object = require_object(encoded, "checkpoint event");
        const std::uint64_t type = require_unsigned(object, "type", "checkpoint event");
        if (type < static_cast<std::uint64_t>(EventType::SessionStarted) ||
            type > static_cast<std::uint64_t>(
                EventType::ExternalObservationReceived)) {
            throw EventStoreError("checkpoint event has an unknown type");
        }
        const std::uint64_t version = require_unsigned(
            object, "version", "checkpoint event");
        if (version != 1)
            throw EventStoreError("checkpoint event has an unsupported version");

        EventRecord record;
        record.sequence = RecordId{
            require_unsigned(object, "sequence", "checkpoint event")};
        if (!record.sequence.valid())
            throw EventStoreError("checkpoint event sequence must be nonzero");
        record.type = static_cast<EventType>(type);
        record.version = static_cast<std::uint16_t>(version);
        record.payload = require_field(object, "payload", "checkpoint event");
        records.push_back(std::move(record));
    }
    return records;
}

bool try_set_entry(Value::Object& target,
                   const EventRecord& record,
                   const char* context) {
    if (record.payload.kind() != Value::Kind::Object)
        return false;
    const Value::Object& payload = require_object(record.payload, context);
    if (!payload.contains("key") || !payload.contains("value"))
        return false;
    set_entry(target, record, context);
    return true;
}

std::string command_key(const Value::Object& payload, const char* context) {
    const std::uint64_t commandId = require_unsigned(payload, "command_id", context);
    return "command:" + std::to_string(commandId);
}

void merge_command(Value::Object& commands,
                   const EventRecord& record,
                   const char* context) {
    const Value::Object& payload = require_object(record.payload, context);
    if (!payload.contains("command_id")) {
        set_entry(commands, record, context);
        return;
    }
    const std::string key = command_key(payload, context);
    Value::Object merged;
    const auto existing = commands.find(key);
    if (existing != commands.end()) {
        if (existing->second.kind() != Value::Kind::Object)
            throw EventStoreError(std::string(context) + " state must be an object");
        merged = existing->second.as_object();
    }
    for (const auto& [name, value] : payload)
        merged.insert_or_assign(name, value);
    commands.insert_or_assign(key, Value(std::move(merged)));
}

void set_observed(Value::Object& observed,
                  const EventRecord& record) {
    if (try_set_entry(observed, record, "observed state"))
        return;

    const Value::Object& payload = require_object(record.payload, "observed state");
    const Value& route = require_field(payload, "route", "observed state");
    const Value& target = require_field(payload, "target", "observed state");
    if (route.kind() != Value::Kind::String || target.kind() != Value::Kind::String)
        throw EventStoreError("observed state route and target must be strings");
    observed.insert_or_assign(
        route.as_string() + ":" + target.as_string(),
        require_field(payload, "observed", "observed state")
    );
}

void set_observed_authority(Value::Object& authority,
                            const EventRecord& record) {
    const Value::Object& payload = require_object(record.payload, "observed state");
    const auto route = payload.find("route");
    const auto target = payload.find("target");
    if (route == payload.end() || target == payload.end() ||
        route->second.kind() != Value::Kind::String ||
        target->second.kind() != Value::Kind::String) {
        return;
    }
    authority.insert_or_assign(
        route->second.as_string() + ":" + target->second.as_string(),
        record.payload);
}

void restore_checkpoint(SerializedWorldState& state, const EventRecord& record) {
    const Value::Object& checkpoint =
        require_object(record.payload, "checkpoint");
    const auto checkpointSession = checkpoint.find("session");
    if (checkpointSession != checkpoint.end()) {
        if (checkpointSession->second.kind() != Value::Kind::UnsignedInteger ||
            SessionId{checkpointSession->second.as_unsigned_integer()} != state.session) {
            throw EventStoreError("checkpoint session does not match replay metadata");
        }
    }
    const auto replayPosition = checkpoint.find("replay_position");
    if (replayPosition != checkpoint.end()) {
        if (replayPosition->second.kind() != Value::Kind::UnsignedInteger)
            throw EventStoreError("checkpoint replay_position must be unsigned");
        state.replayPosition = RecordId{replayPosition->second.as_unsigned_integer()};
    }
    state.topology = optional_object_field(checkpoint, "topology");
    state.components = optional_object_field(checkpoint, "components");
    state.intents = optional_object_field(checkpoint, "intents");
    state.commands = optional_object_field(checkpoint, "commands");
    state.observedState = optional_object_field(checkpoint, "observed_state");
    state.observedAuthority = object_field_or_empty(
        checkpoint, "observed_authority");
    state.configuration = object_field_or_empty(checkpoint, "configuration");
    state.handleGenerations = object_field_or_empty(checkpoint, "handle_generations");
    state.sessionEvents = records_field_or_empty(checkpoint, "session_events");
    state.configurationEvents = records_field_or_empty(
        checkpoint, "configuration_events");
    state.frameEvents = records_field_or_empty(checkpoint, "frame_events");
    state.resolutions = records_field_or_empty(checkpoint, "resolutions");
    state.commandAttempts = records_field_or_empty(checkpoint, "command_attempts");
    state.reports = records_field_or_empty(checkpoint, "reports");
    state.observations = records_field_or_empty(checkpoint, "observations");
    state.failures = records_field_or_empty(checkpoint, "failures");
    state.recoveries = records_field_or_empty(checkpoint, "recoveries");
    state.retentions = records_field_or_empty(checkpoint, "retentions");
    state.scripts = records_field_or_empty(checkpoint, "scripts");
}

}

SerializedWorldState ReplayProjector::project(
    const EventStoreMetadata& metadata,
    std::span<const EventRecord> records) const {
    SerializedWorldState state;
    state.session = metadata.session;
    state.configuration.emplace("engine_version", Value(metadata.engineVersion));
    state.configuration.emplace(
        "feedback_timing",
        Value(metadata.feedbackTiming == FeedbackTiming::Immediate
            ? "immediate" : "deferred")
    );
    RecordId expectedSequence;

    for (std::size_t index = 0; index < records.size(); ++index) {
        const EventRecord& record = records[index];
        if (!record.sequence.valid())
            throw EventStoreError("replay record sequence must be nonzero");

        if (expectedSequence.valid() && record.sequence != expectedSequence)
            throw EventStoreError("replay record sequence gap");

        if (record.version != 1)
            throw EventStoreError("unsupported replay record version");

        switch (record.type) {
        case EventType::SessionStarted:
            state.sessionEvents.push_back(record);
            break;
        case EventType::TopologyChanged:
            if (record.payload.kind() == Value::Kind::Object &&
                record.payload.as_object().contains("removed") &&
                record.payload.as_object().at("removed").kind() ==
                    Value::Kind::Boolean &&
                record.payload.as_object().at("removed").as_boolean()) {
                erase_entry(state.topology, record, "topology");
            } else {
                set_entry(state.topology, record, "topology");
            }
            break;
        case EventType::ComponentMutated:
            set_entry(state.components, record, "component mutation");
            break;
        case EventType::ComponentRemoved:
            erase_entry(state.components, record, "component removal");
            break;
        case EventType::IntentCreated:
            set_entry(state.intents, record, "intent creation");
            break;
        case EventType::IntentDestroyed:
            erase_entry(state.intents, record, "intent destruction");
            break;
        case EventType::CommandIssued:
        case EventType::CommandStatusChanged:
            merge_command(state.commands, record, "command");
            break;
        case EventType::ObservedStateChanged:
            set_observed(state.observedState, record);
            set_observed_authority(state.observedAuthority, record);
            break;
        case EventType::Checkpoint:
            restore_checkpoint(state, record);
            break;
        case EventType::FrameStarted:
        case EventType::FrameCompleted:
            state.frameEvents.push_back(record);
            break;
        case EventType::FrameFailed:
            state.frameEvents.push_back(record);
            state.failures.push_back(record);
            break;
        case EventType::ResolutionSelected:
            state.resolutions.push_back(record);
            break;
        case EventType::CommandAttempted:
            state.commandAttempts.push_back(record);
            break;
        case EventType::ReportReceived:
            state.reports.push_back(record);
            break;
        case EventType::ExternalObservationReceived:
            state.observations.push_back(record);
            break;
        case EventType::Recovery:
            state.recoveries.push_back(record);
            break;
        case EventType::Retention:
            state.retentions.push_back(record);
            break;
        case EventType::ConfigurationChanged:
            state.configurationEvents.push_back(record);
            if (try_set_entry(state.configuration, record, "configuration")) {
                const Value::Object& payload = record.payload.as_object();
                if (require_key(payload, "configuration") == "handle_generations") {
                    const Value& generations = require_field(
                        payload, "value", "configuration");
                    if (generations.kind() != Value::Kind::Object) {
                        throw EventStoreError(
                            "handle_generations configuration must be an object"
                        );
                    }
                    state.handleGenerations = generations.as_object();
                }
            }
            break;
        case EventType::ScriptExecuted:
            state.scripts.push_back(record);
            break;
        }

        state.replayPosition = record.sequence;
        expectedSequence =
            record.sequence.value == std::numeric_limits<std::uint64_t>::max()
                ? RecordId{}
                : RecordId{record.sequence.value + 1};

        if (!expectedSequence.valid() && index + 1 != records.size())
            throw EventStoreError("records follow exhausted replay sequence");
    }

    return state;
}

Value ReplayProjector::checkpoint_payload(const SerializedWorldState& state) const {
    if (!state.session.valid())
        throw EventStoreError("checkpoint state requires a valid session ID");

    Value::Object checkpoint;
    checkpoint.emplace("session", Value(state.session.value));
    checkpoint.emplace("replay_position", Value(state.replayPosition.value));
    checkpoint.emplace("topology", Value(state.topology));
    checkpoint.emplace("components", Value(state.components));
    checkpoint.emplace("intents", Value(state.intents));
    checkpoint.emplace("commands", Value(state.commands));
    checkpoint.emplace("observed_state", Value(state.observedState));
    checkpoint.emplace("observed_authority", Value(state.observedAuthority));
    checkpoint.emplace("configuration", Value(state.configuration));
    checkpoint.emplace("handle_generations", Value(state.handleGenerations));
    checkpoint.emplace("session_events", records_value(state.sessionEvents));
    checkpoint.emplace(
        "configuration_events", records_value(state.configurationEvents));
    checkpoint.emplace("frame_events", records_value(state.frameEvents));
    checkpoint.emplace("resolutions", records_value(state.resolutions));
    checkpoint.emplace("command_attempts", records_value(state.commandAttempts));
    checkpoint.emplace("reports", records_value(state.reports));
    checkpoint.emplace("observations", records_value(state.observations));
    checkpoint.emplace("failures", records_value(state.failures));
    checkpoint.emplace("recoveries", records_value(state.recoveries));
    checkpoint.emplace("retentions", records_value(state.retentions));
    checkpoint.emplace("scripts", records_value(state.scripts));
    return Value(std::move(checkpoint));
}

std::optional<ReplayDivergence> ReplayVerifier::compare(
    std::span<const EventRecord> recorded,
    std::span<const EventRecord> reproduced) const {
    const std::size_t common = std::min(recorded.size(), reproduced.size());

    for (std::size_t index = 0; index < common; ++index) {
        if (recorded[index] != reproduced[index]) {
            return ReplayDivergence{
                index,
                recorded[index],
                reproduced[index],
                "record contents differ"
            };
        }
    }

    if (recorded.size() > common) {
        return ReplayDivergence{
            common,
            recorded[common],
            std::nullopt,
            "reproduced replay ended early"
        };
    }

    if (reproduced.size() > common) {
        return ReplayDivergence{
            common,
            std::nullopt,
            reproduced[common],
            "reproduced replay has an unexpected record"
        };
    }

    return std::nullopt;
}

std::optional<ReplayDivergence> ReplayVerifier::verify(
    std::span<const EventRecord> recorded,
    const ExecutionCallback& execute) const {
    if (!execute) {
        return ReplayDivergence{
            0,
            recorded.empty() ? std::nullopt
                             : std::optional<EventRecord>{recorded.front()},
            std::nullopt,
            "replay execution callback is empty"
        };
    }

    std::size_t reproducedIndex = 0;
    for (std::size_t index = 0; index < recorded.size(); ++index) {
        try {
            std::vector<EventRecord> emitted = execute(recorded[index]);
            for (EventRecord& actual : emitted) {
                if (reproducedIndex >= recorded.size()) {
                    return ReplayDivergence{
                        reproducedIndex,
                        std::nullopt,
                        std::move(actual),
                        "reproduced replay has an unexpected record"
                    };
                }
                if (recorded[reproducedIndex] != actual) {
                    return ReplayDivergence{
                        reproducedIndex,
                        recorded[reproducedIndex],
                        std::move(actual),
                        "record contents differ"
                    };
                }
                ++reproducedIndex;
            }
        } catch (const std::exception& error) {
            return ReplayDivergence{
                reproducedIndex,
                reproducedIndex < recorded.size()
                    ? std::optional<EventRecord>{recorded[reproducedIndex]}
                    : std::nullopt,
                std::nullopt,
                std::string("host replay execution failed: ") + error.what()
            };
        } catch (...) {
            return ReplayDivergence{
                reproducedIndex,
                reproducedIndex < recorded.size()
                    ? std::optional<EventRecord>{recorded[reproducedIndex]}
                    : std::nullopt,
                std::nullopt,
                "host replay execution failed with an unknown exception"
            };
        }
    }
    if (reproducedIndex < recorded.size()) {
        return ReplayDivergence{
            reproducedIndex,
            recorded[reproducedIndex],
            std::nullopt,
            "reproduced replay ended early"
        };
    }
    return std::nullopt;
}

}
