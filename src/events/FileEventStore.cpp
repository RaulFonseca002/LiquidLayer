#include "liquid/events/FileEventStore.hpp"

#include "liquid/events/ValueCodec.hpp"
#include "EventInternals.hpp"
#include "FileEventStoreFormat.hpp"

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

using namespace events_detail;
namespace {

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
#ifdef _WIN32
        // Windows cannot replace a destination that still has an open handle,
        // so the current handle closes first and the untouched original file
        // reopens if the replacement move fails.
        file.reset();
        try {
            replace_file(replacement_path(), path);
        } catch (...) {
            try {
                file = std::make_unique<LockedFile>(path);
            } catch (...) {
                faulted = true;
            }
            throw;
        }
#else
        replace_file(replacement_path(), path);
#endif

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
