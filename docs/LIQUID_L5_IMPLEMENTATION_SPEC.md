# L5 — Exact approval and bounded live operations

**Status:** specified; inactive. **Dependency:** L4 accepted and landed.
Apply [the common contract](LIQUID_IMPLEMENTATION_CONTRACT.md).

## Outcome and authority

A trusted host approves one exact evaluated proposal, then installs it,
replaces a managed behavior, or stops it between Runtime frames. Models can
submit proposals and read permitted facts; they cannot issue approvals or
choose their own grants. These APIs are absent from L6's tool allowlist.

Use existing World lifecycle APIs and L2 SingleReadable scripting. Do not add
a generic World transaction, mutable remote component API, or an LLM resolver.
Inspect `World::destroy_behavior` and committed hardening of membership callback
failure: structural changes may be committed even when callbacks throw.

## Host-only APIs and records

| Method | Required inputs | Result |
| --- | --- | --- |
| `approve` | ProposalId, Passed EvaluationId, current host time, ApprovalPolicy | immutable ApprovalId |
| `activate` | ApprovalId, unique request key, current host time | OperationResult + ManagedBehaviorId |
| `replace` | ManagedBehaviorId, expected revision, ApprovalId, request key, now | OperationResult |
| `stop` | ManagedBehaviorId, expected revision, request key, now | OperationResult |

`ApprovalPolicy` is host-registered data: stable policy name/version, inclusive
monotonic validUntil, and a trusted bounded observational predicate over current
scope/live context. Predicate code is not serialized or model-selected. Deadline
must be finite, no earlier than now, and within the host IntentTime range.
Approval binds the exact proposal, evaluation, fixture version, scope capture,
policy identity, deadline and optional expected managed revision. Changing
policy invalidates its approvals. Evaluation evidence is not authorization.

`ManagedBehaviorRecord` contains a stable Liquid ID, revision starting at 1,
state (`Active`, `Stopped`, `NeedsIntervention`), exact active proposal reference,
and private current BehaviorId/script slot. Liquid records only this control
identity, not a copy of component or intent state. The host owns the script
slot; generated behavior has Read access and never Write to it.

Use the script type supplied at session construction. Add a host-only runner
method `grant_scope(World&, BehaviorId, std::span<const LuaScopeGrant>)` that dispatches to
typed `World::grant_component_access` callbacks on the existing bindings.
Validate the complete target list first, then grant in canonical order.
This is not a topology transaction: apply the partial failure contract below.
It reuses one binding catalog, without a generic model-facing World mutator.

Request keys are nonempty UTF-8 strings up to 96 bytes, unique per session.
Store exact operation arguments with each OperationId. An identical repeated
key returns the recorded historical result without new mutation. Different
arguments under the same key are InvalidInput. Keys are not owner credentials.
An approval permits one operation attempt; retries use the same key. A failed
attempt requires new review/approval before a different activation/replacement.

## Validation immediately before action

Check runtime is outside a frame; reject activate/replace when faulted. Validate
approval identity, caller's native authority, deadline, scope/policy revisions,
target generations, binding identity, Passed evaluation, exact source, expected
managed revision, and the host live-context predicate. Stop remains available
on a faulted runtime because it removes authority; physical outcomes remain
uncertain. Stop does not require an unrevoked authoring scope or approval.

Managed target/source/grants are checked against the last installed mapping.
If trusted native code altered or destroyed them behind the session, return
NeedsIntervention; do not silently adopt that state. Scope expiry/revocation
blocks new activation/replacement, but does not automatically cancel already
active behaviors. The host uses stop for that policy.

Reserve repository, two operation journal records, and (for a new active
behavior) its emergency-stop record pair before topology changes. Failure to
admit returns an error without touching live state or consuming an approval.
Publish OperationStarted immediately before mutation; from that point the
approval is consumed even if the action fails.

## Installation, replacement and stop

**Activate:** allocate one host-owned LuaBehaviorScript component with a unique
name `liquid-script-<session>-<managed>-<revision>` and exact source, create one
behavior, grant only the captured component permissions, then grant Read on its
script slot last. Do not run a frame or source during installation. Publish the
managed mapping only after verifying complete topology. First execution occurs
through the next ordinary Runtime frame. Installation success does not promise
that this frame succeeds or that a device changes.

**Replace:** prepare the successor script slot and a new behavior with its
captured non-script grants, but no script access yet. Between frames, destroy
the old behavior (which removes all its owned intents), verify it is gone,
then grant Read on the successor script and publish the new private handle.
Keep the Liquid managed ID, increment its revision without wrap, and remove
the old host-owned script slot. The new lifecycle starts fresh; common state
components keep their existing values, with no implicit migration/copy.

All old intents are removed, including losing persistent desires. Unrelated
behaviors and their intents survive. This deliberately changes behavior identity
internally: historical records keep the old identity; the Liquid ID correlates
revisions. Do not merely edit source/revision on the old shared script slot.

**Stop:** destroy the managed behavior, verify removal and owned-intent cleanup,
then remove its exclusively host-owned script slot and mark Stopped. Keep the
record for history/idempotency. No pause/resume is implemented. A stopped record
cannot be replaced; starting again requires a new proposal/managed ID.

Destroying an intent cannot recall a dispatched command. Already queued reports
remain subject to normal Runtime correlation/projection. None of these APIs
issues a compensating device command or claims the device is OFF. If that policy
is needed, express it as a separately evaluated/approved behavior.

## Partial failure contract

World topology changes are not a multi-action transaction. Before old-behavior
retirement, attempt cleanup of a failed successor and keep the old mapping only
if inspection confirms it remains intact. After retirement, never resurrect old
intents or report a rolled-back replacement. Attempt cleanup of the successor;
report the actual remaining handles and NeedsIntervention privately to the host.

If callbacks throw, inspect actual liveness rather than assume no mutation.
Any incomplete topology operation marks the managed record NeedsIntervention
and disables further automatic authoring operations on that record. The host
must withhold frame advancement while candidate/old script membership is
uncertain, inspect and explicitly repair or stop it. This is a required host
precondition, not a new Runtime scheduler. Independent healthy Runtime sessions
are unaffected. Journal terminal outcome describes which actions committed;
no generic "activation failed, nothing happened" claim is permitted.

Successful stop can still report a cleanup warning if only an inert script
slot remains; that outcome is NeedsIntervention until host cleanup completes.
An operation cannot change a failed result to success by rerunning its key.

## Implementation steps and tests

| Step | Implement after its failing test | Required oracle |
| --- | --- | --- |
| L5.1 | Exact approvals and idempotent operation admission | Mismatched source/evaluation/policy/scope rejected; stale/deadline/ABA; same-key replay versus changed arguments; capacity rejection before mutation |
| L5.2 | Managed activation | Exact source, only approved grants, private script Read-only, no immediate frame execution; two distinct active scripts; model disappears and ordinary frames still execute |
| L5.3 | Replacement and stop | Old winning and losing persistent intents removed; unrelated intents intact; fresh on_start/watch state; stopped mapping/history retained; pending command feedback is not magically cancelled |
| L5.4 | Failure and emergency capacity | Throw at each topology stage and callback; inspect actual liveness; pre/post-retirement outcomes distinct; no duplicate runnable scripts; NeedsIntervention; reserved stop works when normal journal is full |

Register `authoring_operations`. Run
`ctest --test-dir build/strict -R '^authoring_(operations|journal|inspection|evaluation)$' --output-on-failure`
and all common gates. Tests inspect World intent ownership and actual lifecycle
execution, not only ManagedBehaviorRecord flags.

## Allowed files and exit

New: `include/liquid/authoring/Operations.hpp`, `src/authoring/Operations.cpp`,
`tests/test_authoring_operations.cpp`.
Existing: authoring session/types/journal/view, runner header/implementation for
the typed grant helper, their tests, CMake/consumer/docs.
No Core semantic change or new device adapter is allowed in L5.

Exit: every operation is exact, bounded, scope-checked, evidenced and explicit
about partial topology failure and outstanding physical commands. The owner can
approve progression to optional transport without trusting model assertions.
