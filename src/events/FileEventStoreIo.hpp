#pragma once

#include "liquid/events/EventStore.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
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

namespace liquid::events_detail {

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

inline void replace_file(const std::filesystem::path& replacement,
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

inline void flush_parent_directory(const std::filesystem::path& destination) {
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

}
