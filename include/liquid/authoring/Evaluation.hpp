#pragma once

#include "liquid/Runtime.hpp"
#include "liquid/authoring/Types.hpp"
#include "liquid/events/EventTypes.hpp"
#include "liquid/scripting/LuaLifecycleSystem.hpp"
#include "liquid/simulation/InMemoryAdapter.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace liquid::authoring {

// Host-registered suite identity; the only evaluation input a model chooses.
struct EvaluationFixtureId {
    std::string stableName;
    std::uint32_t version = 0;

    friend bool operator==(const EvaluationFixtureId&, const EvaluationFixtureId&) = default;
    friend auto operator<=>(const EvaluationFixtureId&, const EvaluationFixtureId&) = default;
};

// What the evaluator hands one case factory: the proposal's exact source, the
// captured grants, the case's frozen inputs and adapter behavior.
struct EvaluationPreparation {
    std::string source;
    std::vector<ScopeGrant> grants;
    Value inputs;
    std::vector<simulation::AdapterBehavior> adapters;
    RuntimeOptions runtimeOptions;
};

// The adapter route and target a case factory bound one granted component to
// with Runtime::bind_effect_component.
struct EvaluationEffectAddress {
    TypeName scriptTypeName;
    ComponentName componentName;
    AdapterRoute route;
    EffectTarget target;
};

// One fresh isolated world. Runtime and runner are owned, never copied.
struct PreparedEvaluation {
    std::unique_ptr<Runtime> runtime;
    std::shared_ptr<scripting::LuaBehaviorRunner> runner;
    ComponentType<scripting::LuaBehaviorScript> scriptType;
    BehaviorId candidate{};
    std::vector<std::shared_ptr<simulation::InMemoryAdapter>> adapters;
    // Every effect-bound granted component; each must name a captured grant.
    // Frame summaries count only effects on these addresses.
    std::vector<EvaluationEffectAddress> grantedEffects;
};

struct EvaluationFrameContext {
    std::size_t frameIndex = 0;
    IntentTime now = 0;
    const Value* inputs = nullptr;
    // Null before the frame runs (input actions).
    const FrameResult* frame = nullptr;
    const scripting::LuaExecutionResult* lifecycle = nullptr;
    std::span<const EventRecord> frameRecords;
};

struct EvaluationAssertion {
    bool passed = false;
    std::string diagnostic;
};

// Trusted host callbacks. None may call run_frame or touch I/O.
using EvaluationCaseFactory = std::function<PreparedEvaluation(const EvaluationPreparation&)>;
using EvaluationInputAction = std::function<void(PreparedEvaluation&, const EvaluationFrameContext&)>;
using EvaluationExpectation =
    std::function<EvaluationAssertion(const PreparedEvaluation&, const EvaluationFrameContext&)>;

struct EvaluationCase {
    std::string name;
    Value inputs;
    // Explicit and nondecreasing; the only source of evaluation time.
    std::vector<IntentTime> frameTimes;
    FeedbackTiming feedbackTiming = FeedbackTiming::Deferred;
    std::vector<simulation::AdapterBehavior> adapters;
    EvaluationCaseFactory prepare;
    // Optional.
    EvaluationInputAction input;
    EvaluationExpectation expect;
};

struct EvaluationSuite {
    EvaluationFixtureId id;
    std::vector<EvaluationCase> cases;
};

enum class EvaluationStatus {
    Passed,
    Failed,
    LimitExceeded,
    HostError
};

enum class EvaluationCaseStatus {
    Passed,
    Failed,
    LimitExceeded,
    HostError,
    NotRun
};

struct EvaluationFrameSummary {
    IntentTime now = 0;
    bool frameCompleted = false;
    std::optional<scripting::LuaExecutionStatus> lifecycleStatus;
    std::size_t createdIntents = 0;
    std::size_t cancelledIntents = 0;
    std::size_t liveCandidateIntents = 0;
    bool candidateSelected = false;
    // Effects on granted targets only; observations on readable ones only.
    std::size_t commandsIssued = 0;
    std::vector<CommandStatus> reportStatuses;
    std::size_t observations = 0;
    bool assertionPassed = false;

    friend bool operator==(const EvaluationFrameSummary&, const EvaluationFrameSummary&) = default;
};

struct EvaluationTargetSummary {
    TypeName scriptTypeName;
    ComponentName componentName;
    ComponentAccessMode mode = ComponentAccessMode::Read;
    // Readable grants only.
    std::optional<Value> value;

    friend bool operator==(const EvaluationTargetSummary&, const EvaluationTargetSummary&) = default;
};

struct EvaluationCaseResult {
    std::string name;
    EvaluationCaseStatus status = EvaluationCaseStatus::NotRun;
    std::size_t framesScheduled = 0;
    std::size_t framesRun = 0;
    // At most AuthoringMaxDiagnosticBytes.
    std::string diagnostic;
    std::vector<EvaluationFrameSummary> frames;
    std::vector<EvaluationTargetSummary> finalState;
};

// Immutable public result of one admitted evaluation.
struct EvaluationRecord {
    EvaluationId id;
    AuthoringSessionId session;
    ProposalId proposal;
    ScopeId scope;
    ScopeRevision scopeRevision = 0;
    EvaluationFixtureId fixture;
    EvaluationStatus status = EvaluationStatus::HostError;
    // False when the response bound truncated the case evidence.
    bool complete = false;
    std::vector<EvaluationCaseResult> cases;
};

struct EvaluationTraceEntry {
    std::size_t caseIndex = 0;
    EventType type = EventType::SessionStarted;
    std::uint16_t version = 1;
    // Handles replaced by first-seen ordinals.
    Value payload;

    friend bool operator==(const EvaluationTraceEntry&, const EvaluationTraceEntry&) = default;
};

struct EvaluationTrace {
    EvaluationId evaluation;
    std::vector<EvaluationTraceEntry> entries;
};

// Accepted effects of one frame across the whole isolated world.
struct EvaluationWorldFrameTotals {
    IntentTime now = 0;
    std::size_t commandsIssued = 0;
    std::size_t reports = 0;
    std::size_t observations = 0;

    friend bool operator==(const EvaluationWorldFrameTotals&, const EvaluationWorldFrameTotals&) = default;
};

struct EvaluationWorldCaseTotals {
    // Empty for a case that did not run.
    std::vector<EvaluationWorldFrameTotals> frames;
};

// Host-only whole-world counts, derived from the kept normalized trace.
struct EvaluationWorldTotals {
    EvaluationId evaluation;
    // False when the kept evidence may be truncated: the run budget refused a
    // record, or the response bound dropped the case results.
    bool complete = false;
    // Indexed like EvaluationRecord::cases.
    std::vector<EvaluationWorldCaseTotals> cases;
};

}
