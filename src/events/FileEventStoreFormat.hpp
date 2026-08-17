#pragma once

#include "liquid/events/EventStore.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace liquid::events_detail {

// On-disk format boundary for the file event store. The codec, CRC,
// scanner, and recovery classification live in FileEventStoreFormat.cpp;
// only the operations the store facade needs are exposed here.

struct ParsedHeader {
    EventStoreMetadata metadata;
    std::size_t bytes = 0;
};

struct ScanResult {
    std::vector<EventRecord> records;
    std::size_t validBytes = 0;
    std::size_t invalidTailBytes = 0;
};

std::vector<std::uint8_t> encode_header(const EventStoreMetadata& metadata);
ParsedHeader decode_header(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_batch(std::span<const EventRecord> records);
ScanResult scan_batches(std::span<const std::uint8_t> bytes,
                        std::size_t position);

}
