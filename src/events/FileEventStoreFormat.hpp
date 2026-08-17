#pragma once

#include "EventInternals.hpp"

#include "liquid/events/EventStore.hpp"
#include "liquid/events/ValueCodec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace liquid::events_detail {

inline constexpr std::array<std::uint8_t, 8> fileMagic{
    'L', 'I', 'Q', 'E', 'V', 'T', '1', 0
};
inline constexpr std::array<std::uint8_t, 4> batchMagic{'L', 'B', 'A', 'T'};
inline constexpr std::uint16_t batchFormatVersion = 1;
inline constexpr std::size_t batchPrefixBytes = 32;
inline constexpr std::size_t batchTrailerBytes = 4;

class RecoverableTailError final : public EventStoreError {
public:
    using EventStoreError::EventStoreError;
};

inline std::uint32_t crc32c(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xffffffffU;

    for (std::uint8_t byte : bytes) {
        crc ^= byte;

        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask =
                0U - static_cast<std::uint32_t>(crc & 1U);
            crc = (crc >> 1U) ^ (0x82f63b78U & mask);
        }
    }

    return ~crc;
}

inline void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

inline void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        output.push_back(static_cast<std::uint8_t>(value >> shift));
}

inline void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8)
        output.push_back(static_cast<std::uint8_t>(value >> shift));
}

class ByteReader {
private:
    std::span<const std::uint8_t> bytes;
    std::size_t cursor;
    std::size_t boundary;

public:
    ByteReader(std::span<const std::uint8_t> input,
               std::size_t start,
               std::size_t end)
        : bytes(input), cursor(start), boundary(end) {
        if (start > end || end > input.size())
            throw EventStoreError("invalid binary event-store bounds");
    }

    void require(std::size_t count) const {
        if (count > boundary - cursor)
            throw EventStoreError("truncated binary event-store field");
    }

    std::uint8_t u8() {
        require(1);
        return bytes[cursor++];
    }

    std::uint16_t u16() {
        require(2);
        const std::uint16_t value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[cursor]) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[cursor + 1]) << 8U));
        cursor += 2;
        return value;
    }

    std::uint32_t u32() {
        require(4);
        std::uint32_t value = 0;

        for (unsigned shift = 0; shift < 32; shift += 8)
            value |= static_cast<std::uint32_t>(bytes[cursor++]) << shift;

        return value;
    }

    std::uint64_t u64() {
        require(8);
        std::uint64_t value = 0;

        for (unsigned shift = 0; shift < 64; shift += 8)
            value |= static_cast<std::uint64_t>(bytes[cursor++]) << shift;

        return value;
    }

    std::span<const std::uint8_t> take(std::size_t count) {
        require(count);
        auto result = bytes.subspan(cursor, count);
        cursor += count;
        return result;
    }

    std::size_t position() const {
        return cursor;
    }
};

inline std::vector<std::uint8_t> encode_header(const EventStoreMetadata& metadata) {
    events_detail::validate_metadata(metadata);

    std::vector<std::uint8_t> header(fileMagic.begin(), fileMagic.end());
    append_u16(header, metadata.fileFormatVersion);
    const std::size_t sizePosition = header.size();
    append_u16(header, 0);
    append_u64(header, metadata.session.value);
    header.push_back(static_cast<std::uint8_t>(metadata.feedbackTiming));
    header.insert(header.end(), 3, 0);
    append_u64(header, metadata.fileGeneration);
    append_u16(header, static_cast<std::uint16_t>(metadata.engineVersion.size()));
    header.insert(header.end(), metadata.engineVersion.begin(), metadata.engineVersion.end());

    const std::size_t totalSize = header.size() + sizeof(std::uint32_t);
    if (totalSize > std::numeric_limits<std::uint16_t>::max())
        throw EventStoreError("event-store header limit exceeded");

    const auto encodedSize = static_cast<std::uint16_t>(totalSize);
    header[sizePosition] = static_cast<std::uint8_t>(encodedSize);
    header[sizePosition + 1] = static_cast<std::uint8_t>(encodedSize >> 8U);
    append_u32(header, crc32c(header));
    return header;
}

struct ParsedHeader {
    EventStoreMetadata metadata;
    std::size_t bytes = 0;
};

inline ParsedHeader decode_header(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 39)
        throw EventStoreError("truncated event-store header");

    if (!std::equal(fileMagic.begin(), fileMagic.end(), bytes.begin()))
        throw EventStoreError("invalid event-store magic");

    ByteReader reader(bytes, fileMagic.size(), bytes.size());
    EventStoreMetadata metadata;
    metadata.fileFormatVersion = reader.u16();
    const std::size_t headerBytes = reader.u16();

    if (metadata.fileFormatVersion != EventStoreMetadata::currentFileFormatVersion)
        throw EventStoreError("unsupported event-store file format version");

    if (headerBytes < 39 || headerBytes > bytes.size())
        throw EventStoreError("invalid event-store header size");

    metadata.session = SessionId{reader.u64()};
    if (!metadata.session.valid())
        throw EventStoreError("invalid session ID in event-store header");
    const std::uint8_t timing = reader.u8();

    if (timing > static_cast<std::uint8_t>(FeedbackTiming::Immediate))
        throw EventStoreError("invalid feedback timing in event-store header");

    metadata.feedbackTiming = static_cast<FeedbackTiming>(timing);
    reader.take(3);
    metadata.fileGeneration = reader.u64();

    if (metadata.fileGeneration == 0)
        throw EventStoreError("invalid file generation");

    const std::size_t engineBytes = reader.u16();
    if (engineBytes == 0 || engineBytes > EventLimits::maxEngineVersionBytes ||
        reader.position() + engineBytes + sizeof(std::uint32_t) != headerBytes) {
        throw EventStoreError("invalid engine version in event-store header");
    }

    const auto engine = reader.take(engineBytes);
    metadata.engineVersion.assign(
        reinterpret_cast<const char*>(engine.data()), engine.size());
    const std::uint32_t expectedCrc = reader.u32();
    const std::uint32_t actualCrc = crc32c(bytes.first(headerBytes - 4));

    if (expectedCrc != actualCrc)
        throw EventStoreError("event-store header checksum mismatch");

    events_detail::validate_metadata(metadata);
    return ParsedHeader{std::move(metadata), headerBytes};
}

inline std::vector<std::uint8_t> encode_batch(
    std::span<const EventRecord> records) {
    if (records.empty() || records.size() > EventLimits::maxRecordsPerBatch)
        throw EventStoreError("invalid event batch record count");

    std::vector<std::uint8_t> payload;

    for (const EventRecord& record : records) {
        const auto encodedValue = encode_value(record.payload);
        if (encodedValue.size() > EventLimits::maxRecordPayloadBytes)
            throw EventStoreError("event record payload limit exceeded");

        append_u16(payload, static_cast<std::uint16_t>(record.type));
        append_u16(payload, record.version);
        append_u32(payload, static_cast<std::uint32_t>(encodedValue.size()));
        payload.insert(payload.end(), encodedValue.begin(), encodedValue.end());
    }

    const std::size_t totalSize = batchPrefixBytes + payload.size() + batchTrailerBytes;
    if (totalSize > EventLimits::maxBatchBytes ||
        payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw EventStoreError("event batch size limit exceeded");
    }

    std::vector<std::uint8_t> batch(batchMagic.begin(), batchMagic.end());
    append_u16(batch, batchFormatVersion);
    append_u16(batch, 0);
    append_u32(batch, static_cast<std::uint32_t>(payload.size()));
    append_u32(batch, static_cast<std::uint32_t>(records.size()));
    append_u64(batch, records.front().sequence.value);
    append_u64(batch, records.back().sequence.value);
    batch.insert(batch.end(), payload.begin(), payload.end());
    append_u32(batch, crc32c(batch));
    return batch;
}

inline bool has_batch_magic_after(std::span<const std::uint8_t> bytes,
                           std::size_t position) {
    while (position < bytes.size()) {
        const auto found = std::search(
            bytes.begin() + static_cast<std::ptrdiff_t>(position), bytes.end(),
            batchMagic.begin(), batchMagic.end());
        if (found == bytes.end())
            return false;

        const std::size_t candidate =
            static_cast<std::size_t>(found - bytes.begin());
        position = candidate + 1;
        if (bytes.size() - candidate < batchPrefixBytes + batchTrailerBytes)
            continue;

        try {
            ByteReader reader(bytes, candidate + batchMagic.size(), bytes.size());
            if (reader.u16() != batchFormatVersion || reader.u16() != 0)
                continue;

            const std::size_t payloadBytes = reader.u32();
            const std::size_t recordCount = reader.u32();
            reader.u64();
            reader.u64();
            if (recordCount == 0 || recordCount > EventLimits::maxRecordsPerBatch ||
                payloadBytes > EventLimits::maxBatchBytes -
                                   batchPrefixBytes - batchTrailerBytes) {
                continue;
            }

            const std::size_t batchBytes =
                batchPrefixBytes + payloadBytes + batchTrailerBytes;
            if (batchBytes > bytes.size() - candidate)
                continue;

            ByteReader checksum(bytes, candidate + batchBytes - 4,
                                candidate + batchBytes);
            if (checksum.u32() ==
                crc32c(bytes.subspan(candidate, batchBytes - 4))) {
                return true;
            }
        } catch (const EventStoreError&) {
        }
    }

    return false;
}

struct ScanResult {
    std::vector<EventRecord> records;
    std::size_t validBytes = 0;
    std::size_t invalidTailBytes = 0;
};

inline Value decode_event_payload(std::span<const std::uint8_t> encoded) {
    try {
        return decode_value(encoded);
    } catch (const std::exception& error) {
        throw EventStoreError(
            std::string("invalid encoded event payload: ") + error.what());
    }
}

inline ScanResult scan_batches(std::span<const std::uint8_t> bytes,
                        std::size_t position) {
    ScanResult result;
    result.validBytes = position;
    RecordId expectedSequence;

    while (position < bytes.size()) {
        const std::size_t batchStart = position;
        bool finalTail = false;

        try {
            if (bytes.size() - position < batchPrefixBytes + batchTrailerBytes)
                throw RecoverableTailError("truncated final event batch");

            if (!std::equal(batchMagic.begin(), batchMagic.end(),
                            bytes.begin() + static_cast<std::ptrdiff_t>(position))) {
                throw RecoverableTailError("invalid event batch magic");
            }

            ByteReader reader(bytes, position + batchMagic.size(), bytes.size());
            const std::uint16_t formatVersion = reader.u16();
            const std::uint16_t flags = reader.u16();
            const std::size_t payloadBytes = reader.u32();
            const std::size_t recordCount = reader.u32();
            const RecordId firstSequence{reader.u64()};
            const RecordId lastSequence{reader.u64()};

            if (formatVersion != batchFormatVersion || flags != 0)
                throw EventStoreError("unsupported event batch version or flags");

            if (recordCount == 0 || recordCount > EventLimits::maxRecordsPerBatch)
                throw EventStoreError("invalid event batch record count");

            if (payloadBytes > EventLimits::maxBatchBytes -
                                   batchPrefixBytes - batchTrailerBytes) {
                throw EventStoreError("event batch size limit exceeded");
            }

            const std::size_t batchBytes =
                batchPrefixBytes + payloadBytes + batchTrailerBytes;
            if (batchBytes > bytes.size() - position)
                throw RecoverableTailError("truncated final event batch");

            const std::size_t batchEnd = position + batchBytes;
            ByteReader checksumReader(bytes, batchEnd - 4, batchEnd);
            const std::uint32_t expectedCrc = checksumReader.u32();
            const std::uint32_t actualCrc =
                crc32c(bytes.subspan(position, batchBytes - 4));
            if (expectedCrc != actualCrc)
                throw RecoverableTailError("event batch checksum mismatch");

            if (!firstSequence.valid() ||
                recordCount - 1 >
                    std::numeric_limits<std::uint64_t>::max() -
                        firstSequence.value ||
                lastSequence.value != firstSequence.value + recordCount - 1) {
                throw EventStoreError("invalid event batch sequence range");
            }

            if (expectedSequence.valid() && firstSequence != expectedSequence)
                throw EventStoreError("event record sequence gap");

            const std::size_t payloadEnd = position + batchPrefixBytes + payloadBytes;
            ByteReader payloadReader(bytes, position + batchPrefixBytes, payloadEnd);
            std::vector<EventRecord> decoded;
            decoded.reserve(recordCount);

            for (std::size_t index = 0; index < recordCount; ++index) {
                const auto type = static_cast<EventType>(payloadReader.u16());
                const std::uint16_t version = payloadReader.u16();
                const std::size_t encodedBytes = payloadReader.u32();

                if (static_cast<std::uint16_t>(type) < 1 ||
                    static_cast<std::uint16_t>(type) >
                        static_cast<std::uint16_t>(
                            EventType::ExternalObservationReceived)) {
                    throw EventStoreError("unknown event record type");
                }

                if (version != 1)
                    throw EventStoreError("unsupported event record version");

                if (encodedBytes > EventLimits::maxRecordPayloadBytes)
                    throw EventStoreError("event record payload limit exceeded");

                const auto encoded = payloadReader.take(encodedBytes);
                const Value payload = decode_event_payload(encoded);
                if (type == EventType::Checkpoint)
                    events_detail::validate_checkpoint_projection(payload);

                decoded.emplace_back();
                EventRecord& record = decoded.back();
                record.sequence = RecordId{firstSequence.value + index};
                record.type = type;
                record.version = version;
                record.payload = payload;
            }

            if (payloadReader.position() != payloadEnd)
                throw EventStoreError("event batch has trailing payload bytes");

            if (decoded.size() > EventLimits::maxRecordsRead - result.records.size())
                throw EventStoreError("event-store record count limit exceeded");

            result.records.insert(result.records.end(),
                                  decoded.begin(), decoded.end());
            expectedSequence =
                lastSequence.value == std::numeric_limits<std::uint64_t>::max()
                    ? RecordId{}
                    : RecordId{lastSequence.value + 1};
            position = batchEnd;
            result.validBytes = position;

            if (!expectedSequence.valid() && position != bytes.size())
                throw EventStoreError("records follow exhausted event sequence");

            continue;
        } catch (const RecoverableTailError&) {
            finalTail = !has_batch_magic_after(bytes, batchStart + 1);
            if (!finalTail)
                throw EventStoreError("mid-file event-store corruption");
        }

        result.invalidTailBytes = bytes.size() - batchStart;
        return result;
    }

    return result;
}

}
