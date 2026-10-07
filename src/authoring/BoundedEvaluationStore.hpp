#pragma once

#include "liquid/authoring/Evaluation.hpp"
#include "liquid/events/MemoryEventStore.hpp"
#include "PayloadAccounting.hpp"

#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace liquid::authoring::detail {

// Contract accounting: 8 per numeric scalar, 1 per boolean, byte length for
// strings and bytes, children plus 8 per element or field plus key bytes.
std::size_t logical_bytes(const Value& value);

// One stored record beyond its payload: one element of 4 fields, three of
// them scalars (sequence id, type, version). A normalized trace entry has the
// same shape (case index for sequence id) and handle ordinals keep scalar
// size, so the kept trace costs at most the bytes charged to the run budget.
inline constexpr std::size_t EventHeaderBytes = LogicalElementBytes + 4 * LogicalElementBytes + 3 * LogicalScalarBytes;

// Logical bytes of an EvaluationRecord with no cases for this fixture name.
std::size_t record_header_bytes(std::string_view fixtureName);

// The one run budget of an evaluation, shared by every case store.
struct EvaluationRunBudget {
    std::size_t maxRecords = 0;
    std::size_t maxBytes = 0;
    std::size_t records = 0;
    std::size_t bytes = 0;
    bool exhausted = false;
};

// Disposable per-case evidence store. The run budget is checked before every
// append; on exhaustion nothing is stored, the budget is marked exhausted and
// EventStoreError is thrown.
class BoundedEvaluationStore final : public EventStore {
public:
    BoundedEvaluationStore(EventStoreMetadata metadata, EvaluationRunBudget& runBudget)
        : store(std::move(metadata)), budget(runBudget) {}

    bool exhausted() const {
        return budget.exhausted;
    }

    const EventStoreMetadata& metadata() const override {
        return store.metadata();
    }

    RecordId append(EventData event, Durability durability = Durability::Durable) override {
        const std::size_t bytes = admit(std::span<const EventData>(&event, 1));
        RecordId id = store.append(std::move(event), durability);
        charge(1, bytes);
        return id;
    }

    std::vector<RecordId> append_batch(
        std::span<const EventData> events,
        Durability durability = Durability::Durable
    ) override {
        const std::size_t bytes = admit(events);
        std::vector<RecordId> ids = store.append_batch(events, durability);
        charge(events.size(), bytes);
        return ids;
    }

    std::vector<EventRecord> read_all() const override {
        return store.read_all();
    }

    void flush() override {
        store.flush();
    }

    void retain_from_checkpoint(RecordId checkpointSequence) override {
        store.retain_from_checkpoint(checkpointSequence);
    }

private:
    // Returns the logical bytes of `events`, or throws before any is stored.
    std::size_t admit(std::span<const EventData> events) {
        std::size_t bytes = 0;
        for (const EventData& event : events)
            bytes = saturating_add(bytes, saturating_add(EventHeaderBytes, logical_bytes(event.payload)));
        if (budget.exhausted
            || events.size() > budget.maxRecords - budget.records
            || bytes > budget.maxBytes - budget.bytes) {
            budget.exhausted = true;
            throw EventStoreError("evaluation event-store budget exhausted");
        }
        return bytes;
    }

    void charge(std::size_t records, std::size_t bytes) {
        budget.records += records;
        budget.bytes += bytes;
    }

    MemoryEventStore store;
    EvaluationRunBudget& budget;
};

// One admitted evaluation's captured inputs; all references outlive the run.
struct EvaluationRun {
    const EvaluationSuite& suite;
    const std::string& source;
    const std::vector<ScopeGrant>& grants;
    const scripting::LuaCapabilityManifest& scope;
    const AuthoringLimits& limits;
};

struct EvaluationOutcome {
    EvaluationStatus status = EvaluationStatus::HostError;
    std::vector<EvaluationCaseResult> cases;
    // Host-private normalized evidence of every case that ran.
    std::vector<EvaluationTraceEntry> trace;
    // Logical bytes charged to the run budget.
    std::size_t storeBytes = 0;
};

// Runs every case in order; never throws for case or callback failures.
EvaluationOutcome run_evaluation(const EvaluationRun& run);

// Enforces the response bound in place (complete = false, LimitExceeded) and
// returns the record's logical bytes.
std::size_t bound_response(EvaluationRecord& record, std::size_t limit);

// Whole-world accepted effects per case and frame, read from the kept trace.
// Stores nothing; `complete` is false when the trace or the kept record cannot
// account for every frame that ran.
EvaluationWorldTotals world_totals(const EvaluationRecord& record, const std::vector<EvaluationTraceEntry>& trace);

}
