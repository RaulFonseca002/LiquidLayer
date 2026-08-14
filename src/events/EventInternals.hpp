#pragma once

#include "liquid/events/EventStore.hpp"
#include "liquid/events/ValueCodec.hpp"

#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace liquid::events_detail {

inline void validate_checkpoint_projection(const Value& projection) {
    if (projection.kind() != Value::Kind::Object)
        throw EventStoreError("checkpoint projection must be an object");

    const Value::Object& object = projection.as_object();
    for (const char* name : {
        "topology", "components", "intents", "commands", "observed_state"
    }) {
        const auto field = object.find(name);
        if (field == object.end() || field->second.kind() != Value::Kind::Object) {
            throw EventStoreError(
                std::string("checkpoint projection requires object field: ") + name);
        }
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
