#pragma once

#include "liquid/events/EventStore.hpp"
#include "liquid/events/ValueCodec.hpp"

#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace liquid::events_detail {

inline const Value& checkpoint_field(
    const Value::Object& object,
    const char* name
) {
    const auto field = object.find(name);
    if (field == object.end())
        throw EventStoreError(std::string("checkpoint is missing field: ") + name);
    return field->second;
}

inline const Value::Object& checkpoint_object(
    const Value::Object& object,
    const char* name
) {
    const Value& field = checkpoint_field(object, name);
    if (field.kind() != Value::Kind::Object)
        throw EventStoreError(std::string("checkpoint field must be an object: ") + name);
    return field.as_object();
}

inline const Value::Array& checkpoint_array(
    const Value::Object& object,
    const char* name
) {
    const Value& field = checkpoint_field(object, name);
    if (field.kind() != Value::Kind::Array)
        throw EventStoreError(std::string("checkpoint field must be an array: ") + name);
    return field.as_array();
}

inline std::uint64_t checkpoint_unsigned(
    const Value::Object& object,
    const char* name
) {
    const Value& field = checkpoint_field(object, name);
    if (field.kind() != Value::Kind::UnsignedInteger)
        throw EventStoreError(std::string("checkpoint field must be unsigned: ") + name);
    return field.as_unsigned_integer();
}

inline void validate_checkpoint_record_array(
    const Value::Object& object,
    const char* name
) {
    for (const Value& encoded : checkpoint_array(object, name)) {
        if (encoded.kind() != Value::Kind::Object)
            throw EventStoreError("checkpoint event must be an object");
        const Value::Object& event = encoded.as_object();
        const std::uint64_t sequence = checkpoint_unsigned(event, "sequence");
        const std::uint64_t type = checkpoint_unsigned(event, "type");
        const std::uint64_t version = checkpoint_unsigned(event, "version");
        checkpoint_field(event, "payload");
        if (sequence == 0)
            throw EventStoreError("checkpoint event sequence must be nonzero");
        if (type < static_cast<std::uint64_t>(EventType::SessionStarted) ||
            type > static_cast<std::uint64_t>(
                EventType::ExternalObservationReceived)) {
            throw EventStoreError("checkpoint event has an unknown type");
        }
        if (version != 1)
            throw EventStoreError("checkpoint event has an unsupported version");
    }
}

inline void validate_checkpoint_projection(const Value& projection) {
    if (projection.kind() != Value::Kind::Object)
        throw EventStoreError("checkpoint projection must be an object");

    const Value::Object& object = projection.as_object();
    if (checkpoint_unsigned(object, "session") == 0)
        throw EventStoreError("checkpoint session must be nonzero");
    checkpoint_unsigned(object, "replay_position");

    for (const char* name : {
             "topology", "components", "intents", "commands",
             "observed_state", "observed_authority", "configuration",
             "handle_generations"}) {
        checkpoint_object(object, name);
    }
    for (const auto& [name, value] : checkpoint_object(object, "commands")) {
        static_cast<void>(name);
        if (value.kind() != Value::Kind::Object)
            throw EventStoreError("checkpoint command entry must be an object");
    }
    for (const auto& [name, value] : checkpoint_object(
             object, "observed_authority")) {
        static_cast<void>(name);
        if (value.kind() != Value::Kind::Object)
            throw EventStoreError(
                "checkpoint observed-authority entry must be an object");
    }

    for (const char* name : {
             "session_events", "configuration_events", "frame_events",
             "resolutions", "command_attempts", "reports", "observations",
             "failures", "recoveries", "retentions", "scripts"}) {
        validate_checkpoint_record_array(object, name);
    }
}

inline void validate_checkpoint_session(
    const Value& projection,
    SessionId session
) {
    validate_checkpoint_projection(projection);
    if (SessionId{checkpoint_unsigned(projection.as_object(), "session")} != session)
        throw EventStoreError("checkpoint session does not match event-store metadata");
}

inline void validate_checkpoint_anchor(
    const Value& projection,
    SessionId session,
    RecordId checkpointSequence
) {
    validate_checkpoint_session(projection, session);
    if (!checkpointSequence.valid())
        throw EventStoreError("checkpoint record sequence must be nonzero");
    const std::uint64_t expectedPosition = checkpointSequence.value - 1;
    if (checkpoint_unsigned(projection.as_object(), "replay_position") !=
        expectedPosition) {
        throw EventStoreError("checkpoint replay_position does not precede checkpoint");
    }
}

inline void validate_metadata(const EventStoreMetadata& metadata) {
    if (metadata.fileFormatVersion != EventStoreMetadata::currentFileFormatVersion)
        throw EventStoreError("unsupported event-store file format version");

    if (metadata.engineVersion.empty() ||
        metadata.engineVersion.size() > EventLimits::maxEngineVersionBytes) {
        throw EventStoreError("invalid engine version");
    }

    if (metadata.fileGeneration == 0)
        throw EventStoreError("file generation must be nonzero");

    if (!metadata.session.valid())
        throw EventStoreError("session ID must be nonzero");

    try {
        Value(metadata.engineVersion).validate();
    } catch (const std::exception& error) {
        throw EventStoreError(std::string("invalid engine version: ") + error.what());
    }
}

inline void validate_event(const EventData& event) {
    const auto type = static_cast<std::uint16_t>(event.type);
    if (type < static_cast<std::uint16_t>(EventType::SessionStarted) ||
        type > static_cast<std::uint16_t>(
            EventType::ExternalObservationReceived)) {
        throw EventStoreError("unknown event type");
    }

    if (event.version != 1)
        throw EventStoreError("unsupported event record version");

    if (event.type == EventType::Checkpoint)
        validate_checkpoint_projection(event.payload);

    try {
        const std::vector<std::uint8_t> encoded = encode_value(event.payload);
        if (encoded.size() > EventLimits::maxRecordPayloadBytes)
            throw EventStoreError("event record payload limit exceeded");
    } catch (const EventStoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw EventStoreError(std::string("invalid event payload: ") + error.what());
    }
}

inline Value retention_payload(RecordId firstPruned,
                               RecordId lastPruned,
                               RecordId checkpoint) {
    Value::Object payload;
    payload.emplace("checkpoint", Value(checkpoint.value));
    payload.emplace("first_pruned", Value(firstPruned.value));
    payload.emplace("last_pruned", Value(lastPruned.value));
    return Value(std::move(payload));
}

}
