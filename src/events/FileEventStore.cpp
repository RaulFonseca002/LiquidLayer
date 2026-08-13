#include "liquid/events/FileEventStore.hpp"

#include "liquid/events/ValueCodec.hpp"
#include "EventInternals.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace liquid {
namespace {

constexpr std::array<std::uint8_t, 8> fileMagic{
    'L', 'I', 'Q', 'E', 'V', 'T', '1', 0
};
constexpr std::array<std::uint8_t, 4> batchMagic{'L', 'B', 'A', 'T'};
constexpr std::uint16_t batchFormatVersion = 1;
constexpr std::size_t batchPrefixBytes = 32;
constexpr std::size_t batchTrailerBytes = 4;

class RecoverableTailError final : public EventStoreError {
public:
    using EventStoreError::EventStoreError;
};

std::uint32_t crc32c(std::span<const std::uint8_t> bytes) {
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

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        output.push_back(static_cast<std::uint8_t>(value >> shift));
}

void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
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

std::vector<std::uint8_t> encode_header(const EventStoreMetadata& metadata) {
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

ParsedHeader decode_header(std::span<const std::uint8_t> bytes) {
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

std::vector<std::uint8_t> encode_batch(
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

bool has_batch_magic_after(std::span<const std::uint8_t> bytes,
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

Value decode_event_payload(std::span<const std::uint8_t> encoded) {
    try {
        return decode_value(encoded);
    } catch (const std::exception& error) {
        throw EventStoreError(
            std::string("invalid encoded event payload: ") + error.what());
    }
}

ScanResult scan_batches(std::span<const std::uint8_t> bytes,
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
                        static_cast<std::uint16_t>(EventType::ScriptExecuted)) {
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

class SessionLock {
private:
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int descriptor = -1;
#endif

public:
    explicit SessionLock(const std::filesystem::path& path) {
#ifdef _WIN32
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                             nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                             nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            throw EventStoreError("unable to acquire event-store session lock");

        FILE_ATTRIBUTE_TAG_INFO attributes{};
        BY_HANDLE_FILE_INFORMATION identity{};
        if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo,
                                          &attributes, sizeof(attributes)) ||
            !GetFileInformationByHandle(handle, &identity) ||
            (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            identity.nNumberOfLinks != 1) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            throw EventStoreError("event-store session lock is not a regular file");
        }
#else
        descriptor = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
                            0600);
        if (descriptor < 0)
            throw EventStoreError("unable to open event-store session lock");

        struct stat status {};
        if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
            status.st_nlink != 1 ||
            ::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
            ::close(descriptor);
            descriptor = -1;
            throw EventStoreError("unable to acquire event-store session lock");
        }
#endif
    }

    ~SessionLock() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
#else
        if (descriptor >= 0)
            ::close(descriptor);
#endif
    }

    SessionLock(const SessionLock&) = delete;
    SessionLock& operator=(const SessionLock&) = delete;
};

class LockedFile {
private:
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int descriptor = -1;
#endif

public:
    explicit LockedFile(const std::filesystem::path& path,
                        bool exclusiveCreate = false) {
#ifdef _WIN32
        const DWORD creation = exclusiveCreate ? CREATE_NEW : OPEN_ALWAYS;
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, creation,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                             nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            throw EventStoreError("unable to open or lock event-store file");

        FILE_ATTRIBUTE_TAG_INFO attributes{};
        BY_HANDLE_FILE_INFORMATION identity{};
        if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo,
                                          &attributes, sizeof(attributes)) ||
            !GetFileInformationByHandle(handle, &identity) ||
            (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            identity.nNumberOfLinks != 1) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            throw EventStoreError("event-store path is not a regular file");
        }
#else
        int flags = O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW;
        if (exclusiveCreate)
            flags |= O_EXCL;
        descriptor = ::open(path.c_str(), flags, 0600);
        if (descriptor < 0)
            throw EventStoreError(std::string("unable to open event-store file: ") +
                                  std::strerror(errno));

        struct stat status {};
        if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
            status.st_nlink != 1 ||
            ::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
            ::close(descriptor);
            descriptor = -1;
            throw EventStoreError("event-store path is not a lockable regular file");
        }
#endif
    }

    ~LockedFile() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
#else
        if (descriptor >= 0)
            ::close(descriptor);
#endif
    }

    LockedFile(const LockedFile&) = delete;
    LockedFile& operator=(const LockedFile&) = delete;

    std::uint64_t size() const {
#ifdef _WIN32
        LARGE_INTEGER result{};
        if (!GetFileSizeEx(handle, &result) || result.QuadPart < 0)
            throw EventStoreError("unable to read event-store file size");
        return static_cast<std::uint64_t>(result.QuadPart);
#else
        struct stat status {};
        if (::fstat(descriptor, &status) != 0 || status.st_size < 0)
            throw EventStoreError("unable to read event-store file size");
        return static_cast<std::uint64_t>(status.st_size);
#endif
    }

    std::vector<std::uint8_t> read_all() const {
        const std::uint64_t byteCount = size();
        if (byteCount > EventLimits::maxFileBytes ||
            byteCount > std::numeric_limits<std::size_t>::max()) {
            throw EventStoreError("event-store file size limit exceeded");
        }

        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byteCount));
        std::size_t consumed = 0;

        while (consumed < bytes.size()) {
#ifdef _WIN32
            OVERLAPPED offset{};
            const std::uint64_t absolute = consumed;
            offset.Offset = static_cast<DWORD>(absolute);
            offset.OffsetHigh = static_cast<DWORD>(absolute >> 32U);
            const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
                bytes.size() - consumed, std::numeric_limits<DWORD>::max()));
            DWORD actual = 0;
            if (!ReadFile(handle, bytes.data() + consumed, requested, &actual, &offset) ||
                actual == 0) {
                throw EventStoreError("unable to read event-store file");
            }
            consumed += actual;
#else
            const ssize_t actual = ::pread(descriptor, bytes.data() + consumed,
                                           bytes.size() - consumed,
                                           static_cast<off_t>(consumed));
            if (actual <= 0)
                throw EventStoreError("unable to read event-store file");
            consumed += static_cast<std::size_t>(actual);
#endif
        }

        return bytes;
    }

    void write(std::uint64_t offset, std::span<const std::uint8_t> bytes) {
        if (offset > EventLimits::maxFileBytes ||
            bytes.size() > EventLimits::maxFileBytes - offset) {
            throw EventStoreError("event-store file size limit exceeded");
        }

        std::size_t consumed = 0;
        while (consumed < bytes.size()) {
#ifdef _WIN32
            OVERLAPPED position{};
            const std::uint64_t absolute = offset + consumed;
            position.Offset = static_cast<DWORD>(absolute);
            position.OffsetHigh = static_cast<DWORD>(absolute >> 32U);
            const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
                bytes.size() - consumed, std::numeric_limits<DWORD>::max()));
            DWORD actual = 0;
            if (!WriteFile(handle, bytes.data() + consumed, requested, &actual,
                           &position) || actual == 0) {
                throw EventStoreError("unable to write event-store file");
            }
            consumed += actual;
#else
            const ssize_t actual = ::pwrite(descriptor, bytes.data() + consumed,
                                            bytes.size() - consumed,
                                            static_cast<off_t>(offset + consumed));
            if (actual <= 0)
                throw EventStoreError("unable to write event-store file");
            consumed += static_cast<std::size_t>(actual);
#endif
        }
    }

    void truncate(std::uint64_t byteCount) {
#ifdef _WIN32
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(byteCount);
        if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) ||
            !SetEndOfFile(handle)) {
            throw EventStoreError("unable to truncate event-store file");
        }
#else
        if (::ftruncate(descriptor, static_cast<off_t>(byteCount)) != 0)
            throw EventStoreError("unable to truncate event-store file");
#endif
    }

    void flush() {
#ifdef _WIN32
        if (!FlushFileBuffers(handle))
            throw EventStoreError("unable to durably flush event-store file");
#else
        if (::fsync(descriptor) != 0)
            throw EventStoreError("unable to durably flush event-store file");
#endif
    }
};

void replace_file(const std::filesystem::path& replacement,
                  const std::filesystem::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(replacement.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw EventStoreError("unable to atomically replace event-store file");
    }
#else
    if (::rename(replacement.c_str(), destination.c_str()) != 0)
        throw EventStoreError("unable to atomically replace event-store file");
#endif
}

void flush_parent_directory(const std::filesystem::path& destination) {
#ifdef _WIN32
    static_cast<void>(destination);
#else
    const std::filesystem::path parent = destination.has_parent_path()
        ? destination.parent_path()
        : std::filesystem::path(".");
    const int directory = ::open(parent.c_str(), O_RDONLY | O_CLOEXEC);
    if (directory < 0)
        throw EventStoreError("unable to open event-store parent directory");

    const int syncResult = ::fsync(directory);
    const int closeResult = ::close(directory);
    if (syncResult != 0 || closeResult != 0)
        throw EventStoreError("unable to durably flush event-store replacement");
#endif
}

Value recovery_payload(std::size_t truncatedBytes) {
    Value::Object payload;
    payload.emplace("truncated_bytes",
                    Value(static_cast<std::uint64_t>(truncatedBytes)));
    return Value(std::move(payload));
}

}

class FileEventStore::Implementation {
public:
    std::filesystem::path path;
    EventStoreMetadata metadata;
    FileEventStoreOptions options;
    std::unique_ptr<SessionLock> sessionLock;
    std::unique_ptr<LockedFile> file;
    std::vector<EventRecord> records;
    RecordId nextSequence{1};
    bool faulted = false;

    Implementation(std::filesystem::path filePath,
                   EventStoreMetadata requestedMetadata,
                   FileEventStoreOptions requestedOptions)
        : path(std::move(filePath)),
          metadata(requestedMetadata),
          options(requestedOptions),
          sessionLock(std::make_unique<SessionLock>(path.string() + ".lock")),
          file(std::make_unique<LockedFile>(path)) {
        const std::uint64_t existingBytes = file->size();
        if (existingBytes == 0) {
            const auto header = encode_header(metadata);
            file->write(0, header);
            file->flush();
            return;
        }

        load_existing(requestedMetadata);
    }

    void inject(FileEventStoreFaultPoint point) const {
        if (options.faultInjector)
            options.faultInjector(point);
    }

    std::filesystem::path replacement_path() const {
        return path.string() + ".replacement";
    }

    std::unique_ptr<LockedFile> build_replacement(
        const EventStoreMetadata& replacementMetadata,
        std::span<const EventRecord> replacementRecords) {
        const std::filesystem::path replacement = replacement_path();
        std::error_code removeError;
        std::filesystem::remove(replacement, removeError);
        if (removeError)
            throw EventStoreError("unable to remove stale replacement file");

        auto replacementFile = std::make_unique<LockedFile>(replacement, true);
        try {
            const auto header = encode_header(replacementMetadata);
            replacementFile->write(0, header);
            std::uint64_t offset = header.size();

            for (std::size_t begin = 0; begin < replacementRecords.size();) {
                std::size_t end = std::min(
                    replacementRecords.size(),
                    begin + EventLimits::maxRecordsPerBatch);
                std::vector<std::uint8_t> batch;

                while (end > begin) {
                    try {
                        batch = encode_batch(replacementRecords.subspan(
                            begin, end - begin));
                        break;
                    } catch (const EventStoreError&) {
                        if (end == begin + 1)
                            throw;
                        --end;
                    }
                }

                replacementFile->write(offset, batch);
                offset += batch.size();
                begin = end;
            }

            replacementFile->flush();
            const auto replacementBytes = replacementFile->read_all();
            const ParsedHeader headerCheck = decode_header(replacementBytes);
            const ScanResult scanCheck =
                scan_batches(replacementBytes, headerCheck.bytes);
            if (headerCheck.metadata != replacementMetadata ||
                scanCheck.invalidTailBytes != 0 ||
                !std::equal(scanCheck.records.begin(), scanCheck.records.end(),
                            replacementRecords.begin(), replacementRecords.end())) {
                throw EventStoreError("replacement event-store failed validation");
            }
        } catch (...) {
            replacementFile.reset();
            std::error_code ignored;
            std::filesystem::remove(replacement, ignored);
            throw;
        }

        return replacementFile;
    }

    void inject_replacement_ready(
        FileEventStoreFaultPoint point,
        std::unique_ptr<LockedFile>& replacementFile) const {
        try {
            inject(point);
        } catch (...) {
            replacementFile.reset();
            std::error_code ignored;
            std::filesystem::remove(replacement_path(), ignored);
            throw;
        }
    }

    void install_replacement(
        std::unique_ptr<LockedFile> replacementFile,
        EventStoreMetadata replacementMetadata,
        std::vector<EventRecord> replacementRecords,
        FileEventStoreFaultPoint replacedPoint) {
        replace_file(replacement_path(), path);

        metadata = std::move(replacementMetadata);
        records = std::move(replacementRecords);
        nextSequence = records.empty()
            ? RecordId{1}
            : (records.back().sequence.value ==
                       std::numeric_limits<std::uint64_t>::max()
                   ? RecordId{}
                   : RecordId{records.back().sequence.value + 1});
        file = std::move(replacementFile);

        try {
            inject(replacedPoint);
            flush_parent_directory(path);
        } catch (...) {
            faulted = true;
            throw;
        }
    }

    void load_existing(const EventStoreMetadata& requestedMetadata) {
        const auto bytes = file->read_all();
        const ParsedHeader header = decode_header(bytes);

        if (header.metadata.session != requestedMetadata.session ||
            header.metadata.engineVersion != requestedMetadata.engineVersion ||
            header.metadata.feedbackTiming != requestedMetadata.feedbackTiming) {
            throw EventStoreError("event-store metadata does not match existing file");
        }

        metadata = header.metadata;
        ScanResult scan = scan_batches(bytes, header.bytes);
        records = std::move(scan.records);
        if (!records.empty()) {
            if (records.back().sequence.value ==
                std::numeric_limits<std::uint64_t>::max()) {
                nextSequence = RecordId{};
            }
            else
                nextSequence = RecordId{records.back().sequence.value + 1};
        }

        if (scan.invalidTailBytes != 0) {
            if (!options.writableRecovery)
                throw EventStoreError("invalid final event-store batch");

            if (!nextSequence.valid())
                throw EventStoreError("event record sequence exhausted during recovery");

            std::vector<EventRecord> recovered = records;
            recovered.emplace_back();
            EventRecord& recovery = recovered.back();
            recovery.sequence = nextSequence;
            recovery.type = EventType::Recovery;
            recovery.version = 1;
            recovery.payload = recovery_payload(scan.invalidTailBytes);

            auto replacementFile = build_replacement(metadata, recovered);
            inject_replacement_ready(
                FileEventStoreFaultPoint::RecoveryReplacementReady,
                replacementFile);
            install_replacement(
                std::move(replacementFile), metadata, std::move(recovered),
                FileEventStoreFaultPoint::RecoveryReplaced);
        }
    }

    RecordId append_one(EventData event, Durability durability) {
        const std::array<EventData, 1> batch{event};
        return append_many(batch, durability).front();
    }

    std::vector<RecordId> append_many(std::span<const EventData> events,
                                      Durability durability) {
        if (faulted)
            throw EventStoreError("event store is faulted");

        if (events.empty())
            return {};

        if (events.size() > EventLimits::maxRecordsPerBatch)
            throw EventStoreError("event batch record count limit exceeded");

        if (events.size() > EventLimits::maxRecordsRead - records.size())
            throw EventStoreError("event-store record count limit exceeded");

        if (!nextSequence.valid() ||
            events.size() - 1 >
                std::numeric_limits<std::uint64_t>::max() - nextSequence.value) {
            throw EventStoreError("event record sequence exhausted");
        }

        for (const EventData& event : events)
            events_detail::validate_event(event);

        std::vector<EventRecord> prepared;
        prepared.reserve(events.size());
        std::vector<RecordId> sequences;
        sequences.reserve(events.size());

        for (std::size_t index = 0; index < events.size(); ++index) {
            const RecordId sequence{nextSequence.value + index};
            prepared.emplace_back();
            EventRecord& record = prepared.back();
            record.sequence = sequence;
            record.type = events[index].type;
            record.version = events[index].version;
            record.payload = events[index].payload;
            sequences.push_back(sequence);
        }

        const auto encoded = encode_batch(prepared);
        const std::uint64_t offset = file->size();
        records.reserve(records.size() + prepared.size());
        try {
            file->write(offset, encoded);

            if (durability == Durability::Durable)
                file->flush();
        } catch (...) {
            faulted = true;
            throw;
        }

        records.insert(records.end(), prepared.begin(), prepared.end());
        nextSequence.value += events.size();
        return sequences;
    }

};

FileEventStore::FileEventStore(const std::filesystem::path& path,
                               EventStoreMetadata metadata,
                               FileEventStoreOptions options)
    : implementation(std::make_unique<Implementation>(
          path, std::move(metadata), options)) {
}

FileEventStore::~FileEventStore() = default;
FileEventStore::FileEventStore(FileEventStore&&) noexcept = default;
FileEventStore& FileEventStore::operator=(FileEventStore&&) noexcept = default;

const EventStoreMetadata& FileEventStore::metadata() const {
    return implementation->metadata;
}

RecordId FileEventStore::append(EventData event, Durability durability) {
    return implementation->append_one(std::move(event), durability);
}

std::vector<RecordId> FileEventStore::append_batch(
    std::span<const EventData> events,
    Durability durability) {
    return implementation->append_many(events, durability);
}

std::vector<EventRecord> FileEventStore::read_all() const {
    return implementation->records;
}

void FileEventStore::flush() {
    if (implementation->faulted)
        throw EventStoreError("event store is faulted");

    try {
        implementation->file->flush();
    } catch (...) {
        implementation->faulted = true;
        throw;
    }
}

void FileEventStore::retain_from_checkpoint(RecordId checkpointSequence) {
    auto& state = *implementation;
    if (state.faulted)
        throw EventStoreError("event store is faulted");

    const auto checkpoint = std::find_if(state.records.begin(), state.records.end(),
        [checkpointSequence](const EventRecord& record) {
            return record.sequence == checkpointSequence;
        });

    if (checkpoint == state.records.end() || checkpoint->type != EventType::Checkpoint)
        throw EventStoreError("retention requires a retained checkpoint record");
    events_detail::validate_checkpoint_projection(checkpoint->payload);

    if (state.metadata.fileGeneration == std::numeric_limits<std::uint64_t>::max())
        throw EventStoreError("file generation exhausted");

    if (!state.nextSequence.valid())
        throw EventStoreError("event record sequence exhausted");

    const bool prunedAny = checkpoint != state.records.begin();
    const RecordId firstPruned = prunedAny
        ? state.records.front().sequence
        : RecordId{};
    const RecordId lastPruned = prunedAny
        ? RecordId{checkpointSequence.value - 1}
        : RecordId{};
    std::vector<EventRecord> retained(checkpoint, state.records.end());
    retained.emplace_back();
    EventRecord& retention = retained.back();
    retention.sequence = state.nextSequence;
    retention.type = EventType::Retention;
    retention.version = 1;
    retention.payload = events_detail::retention_payload(
        firstPruned, lastPruned, checkpointSequence);

    EventStoreMetadata replacementMetadata = state.metadata;
    ++replacementMetadata.fileGeneration;
    auto replacementFile = state.build_replacement(replacementMetadata, retained);
    state.inject_replacement_ready(
        FileEventStoreFaultPoint::CompactionReplacementReady,
        replacementFile);
    state.install_replacement(
        std::move(replacementFile), std::move(replacementMetadata),
        std::move(retained), FileEventStoreFaultPoint::CompactionReplaced);
}

}
