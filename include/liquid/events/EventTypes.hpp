#pragma once

#include "liquid/Value.hpp"
#include "liquid/effects/EffectTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace liquid {

enum class Durability : std::uint8_t {
    Buffered,
    Durable
};

enum class EventType : std::uint16_t {
    SessionStarted = 1,
    TopologyChanged = 2,
    ComponentMutated = 3,
    ComponentRemoved = 4,
    IntentCreated = 5,
    IntentDestroyed = 6,
    FrameStarted = 7,
    FrameCompleted = 8,
    FrameFailed = 9,
    ResolutionSelected = 10,
    CommandIssued = 11,
    CommandAttempted = 12,
    CommandStatusChanged = 13,
    ReportReceived = 14,
    ObservedStateChanged = 15,
    Checkpoint = 16,
    Recovery = 17,
    Retention = 18,
    ConfigurationChanged = 19,
    ScriptExecuted = 20
};

struct EventData {
    EventType type = EventType::SessionStarted;
    std::uint16_t version = 1;
    Value payload;
};

struct EventRecord {
    RecordId sequence;
    EventType type = EventType::SessionStarted;
    std::uint16_t version = 1;
    Value payload;

    bool operator==(const EventRecord&) const = default;
};

struct EventStoreMetadata {
    static constexpr std::uint16_t currentFileFormatVersion = 1;

    SessionId session;
    std::string engineVersion;
    FeedbackTiming feedbackTiming = FeedbackTiming::Deferred;
    std::uint64_t fileGeneration = 1;
    std::uint16_t fileFormatVersion = currentFileFormatVersion;

    bool operator==(const EventStoreMetadata&) const = default;
};

struct EventLimits {
    static constexpr std::size_t maxEngineVersionBytes = 64;
    static constexpr std::size_t maxRecordPayloadBytes = 8 * 1024 * 1024;
    static constexpr std::size_t maxBatchBytes = 16 * 1024 * 1024;
    static constexpr std::size_t maxRecordsPerBatch = 4096;
    static constexpr std::size_t maxRecordsRead = 4 * 1024 * 1024;
    static constexpr std::uint64_t maxFileBytes = 512ULL * 1024ULL * 1024ULL;
};

}
