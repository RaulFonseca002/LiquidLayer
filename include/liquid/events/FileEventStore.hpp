#pragma once

#include "liquid/events/EventStore.hpp"

#include <filesystem>
#include <functional>
#include <memory>

namespace liquid {

enum class FileEventStoreFaultPoint {
    RecoveryReplacementReady,
    RecoveryReplaced,
    CompactionReplacementReady,
    CompactionReplaced
};

struct FileEventStoreOptions {
    bool writableRecovery = false;
    std::function<void(FileEventStoreFaultPoint)> faultInjector;
};

class FileEventStore final : public EventStore {
private:
    class Implementation;
    std::unique_ptr<Implementation> implementation;

public:
    // One process-local owner may open a session file at a time. Instances are
    // deliberately not thread-safe; the Runtime is the single writer.
    FileEventStore(const std::filesystem::path& path,
                   EventStoreMetadata metadata,
                   FileEventStoreOptions options = {});
    ~FileEventStore() override;

    FileEventStore(const FileEventStore&) = delete;
    FileEventStore& operator=(const FileEventStore&) = delete;
    FileEventStore(FileEventStore&&) noexcept;
    FileEventStore& operator=(FileEventStore&&) noexcept;

    const EventStoreMetadata& metadata() const override;
    RecordId append(EventData event,
                    Durability durability = Durability::Durable) override;
    std::vector<RecordId> append_batch(
        std::span<const EventData> events,
        Durability durability = Durability::Durable) override;
    std::vector<EventRecord> read_all() const override;
    void flush() override;
    void retain_from_checkpoint(RecordId checkpointSequence) override;
};

}
