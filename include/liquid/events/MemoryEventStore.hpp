#pragma once

#include "liquid/events/EventStore.hpp"

#include <cstddef>
#include <vector>

namespace liquid {

class MemoryEventStore final : public EventStore {
private:
    EventStoreMetadata storeMetadata;
    std::vector<EventRecord> records;
    std::size_t maximumRecords;
    RecordId nextSequence{1};

public:
    explicit MemoryEventStore(
        EventStoreMetadata metadata,
        std::size_t maxRecords = EventLimits::maxRecordsRead);

    const EventStoreMetadata& metadata() const override;
    RecordId append(EventData event,
                    Durability durability = Durability::Durable) override;
    std::vector<RecordId> append_batch(
        std::span<const EventData> events,
        Durability durability = Durability::Durable) override;
    std::vector<EventRecord> read_all() const override;
    void flush() override;
    void retain_from_checkpoint(RecordId checkpointSequence) override;
#ifdef LIQUID_ENABLE_LEGACY_INTERNAL_COMPONENT_REGISTRATION
    void set_next_sequence_for_test(RecordId sequence) {
        nextSequence = sequence;
    }
#endif
};

}
