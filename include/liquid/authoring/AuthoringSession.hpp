#pragma once

#include "liquid/Runtime.hpp"
#include "liquid/authoring/Evaluation.hpp"
#include "liquid/authoring/Types.hpp"
#include "liquid/scripting/LuaCapabilityManifest.hpp"
#include "liquid/scripting/LuaLifecycleSystem.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace liquid::authoring {

// Copied current manifest of one scope revision.
struct ScopeView {
    ScopeId scope;
    ScopeRevision revision = 0;
    scripting::LuaCapabilityManifest manifest;
};

// External submission. The typed fields are the complete boundary: unknown
// fields cannot be represented. The managed pair is both-or-neither.
struct ProposalSubmission {
    ScopeId scope;
    ScopeRevision expectedScopeRevision = 0;
    // Exact UTF-8 bytes; never trimmed, normalized or re-rendered.
    std::string source;
    // Untrusted data, never instructions.
    std::string rationale;
    std::optional<ManagedBehaviorId> managedBehavior;
    std::optional<ManagedBehaviorRevision> expectedManagedRevision;
};

struct ManagedRevisionExpectation {
    ManagedBehaviorId behavior;
    ManagedBehaviorRevision expectedRevision = 0;
};

// Immutable public record; lookups return copies. A repair is a new proposal.
struct BehaviorProposal {
    ProposalId id;
    AuthoringSessionId session;
    ScopeId scope;
    ScopeRevision scopeRevision = 0;
    std::string contractVersion;
    std::string source;
    std::string rationale;
    // Absent: the proposal describes a new behavior.
    std::optional<ManagedRevisionExpectation> managed;
};

// Owner-thread session over one live Runtime and its configured Lua runner.
// Both outlive the session. Calls run between frames. No call executes Lua,
// creates a behavior or intent, or changes World topology.
//
// A revoked scope is erased: later discover/submit/replace/revoke for it
// return NotFound, as for an ID that never existed.
class AuthoringSession {
public:
    // Throws std::invalid_argument when `scriptType` does not belong to
    // `runtime.world()` or `limits` exceed the contract defaults or hold a zero.
    AuthoringSession(
        AuthoringSessionId id,
        Runtime& runtime,
        scripting::LuaBehaviorRunner& runner,
        ComponentType<scripting::LuaBehaviorScript> scriptType,
        AuthoringLimits limits = {}
    );
    ~AuthoringSession();

    AuthoringSession(const AuthoringSession&) = delete;
    AuthoringSession& operator=(const AuthoringSession&) = delete;
    AuthoringSession(AuthoringSession&&) = delete;
    AuthoringSession& operator=(AuthoringSession&&) = delete;

    const AuthoringSessionId& id() const;
    const AuthoringLimits& limits() const;

    // Trusted host. `owner` is the caller identity later bound into CallerContext.
    AuthoringResult<ScopeView> create_scope(
        std::string owner,
        std::vector<ScopeGrant> grants,
        IntentTime now
    );

    // Trusted host. Complete replacement; the revision increments even when
    // grants compare equal. A failed replacement leaves the scope unchanged.
    AuthoringResult<ScopeView> replace_scope(
        ScopeId scope,
        ScopeRevision expectedRevision,
        std::vector<ScopeGrant> grants,
        IntentTime now
    );

    // Trusted host. Does not stop behaviors that are already active.
    AuthoringResult<void> revoke_scope(ScopeId scope, ScopeRevision expectedRevision);

    // Re-resolves every target and returns fresh copies captured at `now`.
    AuthoringResult<ScopeView> discover(const CallerContext& caller, ScopeId scope, IntentTime now);

    // Admission only: validates bytes, bounds, ownership, revision and target
    // liveness. Does not claim the source parses or runs.
    AuthoringResult<ProposalId> submit(const CallerContext& caller, ProposalSubmission submission);

    AuthoringResult<BehaviorProposal> proposal(const CallerContext& caller, ProposalId proposal) const;

    // Trusted host. Only before the first accepted proposal; a failed
    // registration registers nothing.
    AuthoringResult<void> register_evaluation_suite(EvaluationSuite suite);

    // Runs the proposal's exact source against every case of a registered
    // suite in fresh isolated Runtimes. The live World and session clock are
    // never touched. Errors mean no record was created; once admitted, every
    // outcome is a record.
    AuthoringResult<EvaluationRecord> evaluate(
        const CallerContext& caller,
        ProposalId proposal,
        EvaluationFixtureId fixture
    );

    // Trusted host only.
    AuthoringResult<EvaluationTrace> normalized_trace(EvaluationId evaluation) const;

    // Trusted host only. Never part of an EvaluationRecord.
    AuthoringResult<EvaluationWorldTotals> world_totals(EvaluationId evaluation) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}
