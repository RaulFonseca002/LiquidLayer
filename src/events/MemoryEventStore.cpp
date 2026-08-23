#include "liquid/events/MemoryEventStore.hpp"
#include "liquid/events/Replay.hpp"

#include "EventInternals.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace liquid {

MemoryEventStore::MemoryEventStore(EventStoreMetadata metadata,
                                   std::size_t maxRecords)
    : storeMetadata(std::move(metadata)), maximumRecords(maxRecords) {
    events_detail::validate_metadata(storeMetadata);

    if (maximumRecords == 0 || maximumRecords > EventLimits::maxRecordsRead)
        throw EventStoreError("invalid memory event-store capacity");
}

const EventStoreMetadata& MemoryEventStore::metadata() const {
    return storeMetadata;
}

RecordId MemoryEventStore::append(EventData event, Durability durability) {
    const std::array<EventData, 1> events{std::move(event)};
    return append_batch(events, durability).front();
}

std::vector<RecordId> MemoryEventStore::append_batch(
    std::span<const EventData> events,
    Durability durability) {
    static_cast<void>(durability);

    if (events.empty())
        return {};

    if (events.size() > EventLimits::maxRecordsPerBatch ||
        events.size() > maximumRecords - records.size()) {
        throw EventStoreError("memory event-store capacity exceeded");
    }

    if (!nextSequence.valid() ||
        events.size() - 1 >
            std::numeric_limits<std::uint64_t>::max() - nextSequence.value) {
        throw EventStoreError("event record sequence exhausted");
    }

    for (const EventData& event : events)
        events_detail::validate_event(event);

    std::vector<RecordId> sequences;
    sequences.reserve(events.size());
    records.reserve(records.size() + events.size());

    for (const EventData& event : events) {
        const RecordId sequence = nextSequence;
        ++nextSequence.value;
        records.push_back(EventRecord{sequence, event.type, event.version, event.payload});
        sequences.push_back(sequence);
    }

    return sequences;
}

std::vector<EventRecord> MemoryEventStore::read_all() const {
    return records;
}

void MemoryEventStore::flush() {
}

void MemoryEventStore::retain_from_checkpoint(RecordId checkpointSequence) {
    const auto checkpoint = std::find_if(records.begin(), records.end(),
        [checkpointSequence](const EventRecord& record) {
            return record.sequence == checkpointSequence;
        });

    if (checkpoint == records.end() || checkpoint->type != EventType::Checkpoint)
        throw EventStoreError("retention requires a retained checkpoint record");
    events_detail::validate_checkpoint_anchor(
        checkpoint->payload, storeMetadata.session, checkpointSequence);
    const std::size_t checkpointIndex =
        static_cast<std::size_t>(checkpoint - records.begin());
    ReplayProjector{}.project(
        storeMetadata,
        std::span<const EventRecord>{records.data(), checkpointIndex + 1});

    const std::size_t retainedCount =
        static_cast<std::size_t>(records.end() - checkpoint);
    if (retainedCount >= maximumRecords)
        throw EventStoreError("memory event-store capacity exceeded by retention record");

    if (storeMetadata.fileGeneration == std::numeric_limits<std::uint64_t>::max())
        throw EventStoreError("file generation exhausted");

    if (!nextSequence.valid())
        throw EventStoreError("event record sequence exhausted");

    const bool prunedAny = checkpoint != records.begin();
    const RecordId firstPruned = prunedAny ? records.front().sequence : RecordId{};
    const RecordId lastPruned = prunedAny
        ? RecordId{checkpointSequence.value - 1}
        : RecordId{};
    EventData retention{
        EventType::Retention,
        1,
        events_detail::retention_payload(
            firstPruned, lastPruned, checkpointSequence)
    };
    events_detail::validate_event(retention);

    std::vector<EventRecord> replacement(checkpoint, records.end());
    replacement.emplace_back();
    EventRecord& retentionRecord = replacement.back();
    retentionRecord.sequence = nextSequence;
    retentionRecord.type = retention.type;
    retentionRecord.version = retention.version;
    retentionRecord.payload = std::move(retention.payload);
    EventStoreMetadata replacementMetadata = storeMetadata;
    ++replacementMetadata.fileGeneration;
    RecordId replacementSequence = nextSequence;
    ++replacementSequence.value;

    records = std::move(replacement);
    storeMetadata = std::move(replacementMetadata);
    nextSequence = replacementSequence;
}

}
