#pragma once

#include "liquid/events/EventTypes.hpp"

#include <span>
#include <stdexcept>
#include <vector>

namespace liquid {

class EventStoreError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class EventStore {
public:
    virtual ~EventStore() = default;

    virtual const EventStoreMetadata& metadata() const = 0;
    virtual RecordId append(EventData event,
                            Durability durability = Durability::Durable) = 0;
    virtual std::vector<RecordId> append_batch(
        std::span<const EventData> events,
        Durability durability = Durability::Durable) = 0;
    virtual std::vector<EventRecord> read_all() const = 0;
    virtual void flush() = 0;

    virtual RecordId checkpoint(Value serializedProjection,
                                Durability durability = Durability::Durable);
    virtual void retain_from_checkpoint(RecordId checkpointSequence) = 0;
};

}
