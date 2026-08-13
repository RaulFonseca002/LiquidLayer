#pragma once

#include "liquid/events/EventTypes.hpp"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace liquid {

struct SerializedWorldState {
    SessionId session;
    RecordId replayPosition;
    Value::Object topology;
    Value::Object components;
    Value::Object intents;
    Value::Object commands;
    Value::Object observedState;
    Value::Object observedAuthority;
    Value::Object configuration;
    Value::Object handleGenerations;
    std::vector<EventRecord> sessionEvents;
    std::vector<EventRecord> configurationEvents;
    std::vector<EventRecord> frameEvents;
    std::vector<EventRecord> resolutions;
    std::vector<EventRecord> commandAttempts;
    std::vector<EventRecord> reports;
    std::vector<EventRecord> failures;
    std::vector<EventRecord> recoveries;
    std::vector<EventRecord> retentions;
    std::vector<EventRecord> scripts;

    bool operator==(const SerializedWorldState&) const = default;
};

class ReplayProjector {
public:
    SerializedWorldState project(
        const EventStoreMetadata& metadata,
        std::span<const EventRecord> records) const;

    Value checkpoint_payload(const SerializedWorldState& state) const;
};

struct ReplayDivergence {
    std::size_t recordIndex = 0;
    std::optional<EventRecord> expected;
    std::optional<EventRecord> actual;
    std::string reason;
};

class ReplayVerifier {
public:
    using ExecutionCallback =
        std::function<std::vector<EventRecord>(const EventRecord& evidence)>;

    std::optional<ReplayDivergence> compare(
        std::span<const EventRecord> recorded,
        std::span<const EventRecord> reproduced) const;

    std::optional<ReplayDivergence> verify(
        std::span<const EventRecord> recorded,
        const ExecutionCallback& execute) const;
};

}
