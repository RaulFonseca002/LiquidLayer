#pragma once

#include "liquid/Runtime.hpp"
#include "liquid/events/MemoryEventStore.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace liquid::detail {

using TargetKey = std::pair<std::string, std::string>;

inline TargetKey target_key(const AdapterRoute& route, const EffectTarget& target) {
    return {route.value(), target.value()};
}

inline const char* status_name(CommandStatus status) {
    switch (status) {
    case CommandStatus::Pending: return "pending";
    case CommandStatus::Applied: return "applied";
    case CommandStatus::Rejected: return "rejected";
    case CommandStatus::Failed: return "failed";
    case CommandStatus::TimedOut: return "timed-out";
    case CommandStatus::Superseded: return "superseded";
    case CommandStatus::Indeterminate: return "indeterminate";
    }
    return "unknown";
}

// Inverse of status_name for the same seven wire strings; keep the two
// tables adjacent so a status change cannot update one without the other.
inline CommandStatus parse_status(const std::string& value) {
    if (value == "pending") return CommandStatus::Pending;
    if (value == "applied") return CommandStatus::Applied;
    if (value == "rejected") return CommandStatus::Rejected;
    if (value == "failed") return CommandStatus::Failed;
    if (value == "timed-out") return CommandStatus::TimedOut;
    if (value == "superseded") return CommandStatus::Superseded;
    if (value == "indeterminate") return CommandStatus::Indeterminate;
    throw EventStoreError("recorded command status is invalid");
}

inline Value event_payload(std::initializer_list<std::pair<const std::string, Value>> fields) {
    Value::Object object;
    for (const auto& [name, value] : fields)
        object.emplace(name, value);
    return Value{std::move(object)};
}

inline bool active(CommandStatus status) {
    return status == CommandStatus::Pending || status == CommandStatus::Indeterminate;
}

class RuntimeEffectsState {
public:
    explicit RuntimeEffectsState(RuntimeOptions options);

    SessionId session_id() const;
    FeedbackSender feedback_sender() const;
    std::optional<Value> observed_state(
        const AdapterRoute& route,
        const EffectTarget& target
    ) const;
    std::optional<CommandStatus> command_status(CommandId commandId) const;
    void reconcile_indeterminate(
        const AdapterRoute& route,
        const EffectTarget& target,
        Value observedValue
    );
    RecordId checkpoint();
    void flush();
    void prune_terminal_history(bool reserveCommandSlot = false);
    void append(EventType type, Value payload,
                Durability durability = Durability::Buffered);
    void register_adapter(std::shared_ptr<EffectAdapter> adapter);
    void validate_effects(std::span<const ResolvedEffect> effects) const;
    void process_feedback(
        std::vector<EffectReport>& accepted,
        std::vector<ExternalObservation>& observations
    );
    bool report_is_authoritative(const EffectReport& report) const;
    bool observation_is_authoritative(
        const ExternalObservation& observation
    ) const;

    void retry_due(std::uint64_t now, std::vector<EffectReport>& accepted);
    std::optional<EffectCommand> issue(const ResolvedEffect& effect,
                                       std::uint64_t now,
                                       std::vector<EffectReport>& accepted);
    void clear_desire(const AdapterRoute& route, const EffectTarget& target);

private:
    struct CommandState {
        EffectCommand command;
        std::shared_ptr<EffectAdapter> adapter;
        AdapterCapabilities capabilities;
        CommandStatus status = CommandStatus::Pending;
        std::size_t retryIndex = 0;
        std::optional<EffectReport> terminalReport;
    };

    SessionId session;
    FeedbackTiming timing;
    RetryPolicy retry;
    FeedbackChannel feedback;
    std::unique_ptr<EventStore> ownedStore;
    EventStore* store = nullptr;
    std::map<std::string, std::shared_ptr<EffectAdapter>> adapters;
    std::map<std::uint64_t, CommandState> commands;
    std::map<TargetKey, std::uint64_t> latestByTarget;
    std::map<TargetKey, std::uint64_t> authoritativeByTarget;
    std::map<TargetKey, Value> observed;
    std::map<TargetKey, StateRevision> observedRevisions;
    std::deque<std::uint64_t> terminalOrder;
    std::deque<EffectReport> deferredSynchronousReports;
    std::size_t maximumRetainedCommands;
    std::size_t maximumDeferredReports;
    std::uint64_t nextCommand = 1;

    void restore(std::span<const EventRecord> records);
    void restore_command(
        const Value::Object& object,
        std::uint64_t& maximumCommandId
    );
    void restore_attempt(const Value::Object& object);
    void restore_observed_record(const Value::Object& object);
    void restore_observed_checkpoint(
        const Value::Object& authority,
        const Value::Object& values
    );
    void set_status(CommandState& state, CommandStatus status);
    void transition(
        CommandState& state,
        CommandStatus status,
        const std::string& reason = {}
    );
    void record_status(const CommandState& state, const std::string& reason = {});
    void record_report(
        const EffectReport& report,
        const char* disposition
    );
    void commit_authoritative(
        const AdapterRoute& route,
        const EffectTarget& target,
        const Value& observedValue,
        StateRevision revision,
        std::uint64_t commandId
    );
    void apply_report(const EffectReport& report, std::vector<EffectReport>& accepted);
    void apply_observation(
        const ExternalObservation& observation,
        std::vector<ExternalObservation>& accepted
    );
    void dispatch(CommandState& state, std::uint64_t now,
                  std::vector<EffectReport>& accepted);
};

}
