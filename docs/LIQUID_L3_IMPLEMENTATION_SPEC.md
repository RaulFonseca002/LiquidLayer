# L3 — Scoped runtime truth and frame evidence

**Status:** specified; inactive. **Dependency:** L2 accepted and landed.
Apply [the common contract](LIQUID_IMPLEMENTATION_CONTRACT.md).

## Outcome and existing evidence

Return current permitted state and intent facts without trusting conversation
memory or treating the last frame's selections as current resolver output.
Read `World::intent`, `intents_for`, `intents_owned_by`, `Runtime::last_frame_log`,
`observed_state`, `command_status`, and private external component bindings.

World already exposes copied intent IDs and encoded intent values. Missing
pieces are bounded semantic target descriptions and safe component-to-external
binding inspection. Add read-only Core helpers; do not expose registries or
reconstruct authoritative state by replaying an unrelated UI trace.

> **Baseline note (2026-09-26).** Current `main` already includes `2aee8a4`,
> which makes a recreated component immediately reclaim its effect target, so
> stale effect-binding authority is fixed and is not L3 work. It does not
> change `FrameLog`: last-frame intent selections remain keyed by component
> type/name without target provenance. The name-keyed-to-provenance change
> below therefore remains proposed and unimplemented.

## Core additions

Add `include/liquid/Inspection.hpp` with copied host DTOs and finite limits:

```cpp
WorldInspection World::inspect_targets(
    std::span<const ComponentTarget> targets, InspectionLimits limits) const;
RuntimeInspection Runtime::inspect_targets(
    std::span<const ComponentTarget> targets, InspectionLimits limits) const;
```

`InspectionLimits` defaults: 128 targets, 4,096 intents, 1 MiB logical payload.
Reject duplicate, invalid, stale or cross-world targets. No partial DTO on
overflow/codec failure. These are trusted native APIs, not authorization APIs.
World's DTO contains, per target: current component schema name/version,
component name, copied current encoded Value, copied live `Intent` records and
their current owner handles. Runtime's DTO adds fault state, last-frame
metadata with selections restricted to the requested targets, and current
external binding route/target with optional authoritative
`observed_state` and its current StateRevision. Absent observed state means
unknown, never the desired value. Per bound target include the latest retained
command and, when different, the authoritative retained command: copied command
identity, attempt, status and optional accepted terminal report status. Return
`HistoryUnavailable` when the relevant entry is no longer retained or was never
recorded; do not infer that no command occurred from an empty bounded cache.

Add a src-private `RuntimeEffectsState::inspect_target` query in
`RuntimeInternals.hpp` to compose these copies from `latestByTarget`,
`authoritativeByTarget`, `commands`, `observed` and `observedRevisions`. It does
not expose those containers or add a second command history. Model projection
replaces command handles with capture-local references and removes routes,
payloads and native terminal-report details. Read permission is required for
these target-level command/report facts. L3 promises retained summaries, not
an exhaustive effect timeline or evidence that a dispatched command was applied.
The model view omits global system/intent counts and unscoped failure text;
faults outside its permitted behavior evidence render a generic failed status.
Native host inspection can retain bounded diagnostic detail.

Runtime capture runs only on its owner thread outside a frame; reject reentrant
capture during system/adapter callbacks. The capture calls World inspection and
reads binding/observation state within the same synchronous operation. Trusted
codec encoders used here must not mutate/reenter the World. Invalidated external
bindings are omitted only with an explicit `Unbound` status, not stale values.
No new resolver execution, state mutation, event record or topology callback.
Use the existing internal `component_name`, `encode_component` and intent
target indexes without first copying an unbounded list. Add only the missing
const internal `component_schema(ComponentTypeId)` overload on ComponentRegistry
and its Coordinator forwarding overload to obtain schema name/version. They
return existing registered schema metadata and expose no mutable registry state.

Raw handles/routes in these host DTOs never cross the model boundary. The
session maps them through its scope. Existing Core APIs and Event Format v1
remain source/behavior compatible; only new header/query APIs are added.

## Liquid view API and consistency

Add `AuthoringSession::inspect(CallerContext, ScopeId, IntentTime now)` returning
`RuntimeView`. It contains session/scope/revision, monotonic capture ordinal,
capture time, current target facts, fault status, and a separately labeled
`lastFrame` section. Capture ordinal is a non-reused uint64; not a frame number.

Current facts are captured together. Last-frame selections are historical:
include that frame's number, time and completed/failed status. Never label them
"selected now" after between-frame cancellation, grant changes or replacement.
Per live intent, `selectedInLastFrame` is true/false only if that frame contains
a resolution for this exact target generation; otherwise it is `Unknown`.
If last-frame evidence names an intent now destroyed, show `winnerStillLive=false`.
Do not call the resolver during inspection to manufacture a current winner.

FrameLog maps selections by numeric type/name, which alone cannot prove target
generation. Runtime inspection must retain a private target-identity mapping
when a frame finishes (completed or failed), alongside the last frame log.
This is provenance for the existing last-frame result, not a second resolver
or long-lived state replica. On frame failure, report only selections actually
recorded. Add focused generation/recreation and failed-frame regressions.

This avoids needing a new global mutation epoch. The host calls inspection
between mutations; it does not promise all current values equal their previous
frame values. Pending async reports are not observed truth until Runtime drains
and validates them on a frame.

## Privacy and encoding

Every target must belong to the caller's live scope. Read permission is required
for actual values, observation values and another behavior's desired values.
Write-only views contain target identity/access and explicitly permitted behavior
intent metadata without values. They do not leak hidden initial sensor state.

L3 adds host-only `set_inspection_behaviors(ScopeId, expectedRevision,
span<const BehaviorId>)`. Validate existing world-bound behavior identities,
replace the full list and increment scope revision. This supports existing
native/Lua behaviors before L5 exists; it does not create a managed behavior.
The list defaults to empty. Only future L5 behaviors created by this same scope
also satisfy the policy automatically; replacing their private handle does not
grant access to an unrelated behavior or change the scope policy revision.

For readable targets, show permitted intents with name, priority, lifetime,
live state and selection evidence. Other intents are competitors: return
`hiddenCompetitor=true` and the visible intent's selectedInLastFrame fact,
without hidden names, counts, desired values or raw handles. A destroyed native
allowlist member grants no access to a later recycled handle. The fixture host
explicitly adds FocusSupport to this allowlist for the L3 acceptance scenario.

Within a view, assign opaque intent references by deterministic ordinal. These
references and native behavior references are scoped to the capture and accepted
by no mutation API. From L5, include an optional session-scoped ManagedBehaviorId
for behaviors actually created through that session; it stays stable across
replacement. L3 must not invent L5 mappings for existing native behaviors.

`Intent::encodedValue` uses ComponentCodec, not LuaComponentCodec. Do not label
it with a Lua schema directly. Extend private runner binding helpers to decode
that copied value with the registered ComponentCodec and encode it through the
Lua read codec. Validate against readSchema, and label it "decoded desired
component", distinct from the original submitted write value and current
observed value. Emit only for readable, permitted intents. Fail the scoped
view on conversion failure rather than substitute the wrong schema.

## Implementation steps and tests

| Step | Implement after its failing test | Required oracle |
| --- | --- | --- |
| L3.1 | Bounded World/Runtime copied queries | Target names/schemas/intent data correct; stale handle, cross-world and bounds rejection; owner-thread/no reentry; no state/event mutation |
| L3.2 | Last-frame target provenance | Removed/recreated name never inherits old selection; current intents and past winner labeled separately; failed frame log preserved |
| L3.3 | Scope projection and codec conversion | Read versus Write visibility; asymmetric codecs; hidden competitor redaction; no raw handles/routes or borrowed data |
| L3.4 | End-to-end conflict truth | Persistent 70 loses to 0, survives and wins later; cancellation between frames leaves historical winner labeled; delayed/rejected feedback never appears as applied observation; latest versus authoritative command and pruned history remain distinct |

Register `authoring_inspection`, extend runtime/world/effects tests. Run
`ctest --test-dir build/strict -R '^(authoring_inspection|world|runtime|runtime_effects)$' --output-on-failure`
and all common gates, including Core-only consumers and coverage for the new
Core query branches. Distinguish "not resolved" from a legitimate false value.

## Allowed files and exit

New: `include/liquid/Inspection.hpp`, `src/runtime/RuntimeInspection.cpp`,
`include/liquid/authoring/RuntimeView.hpp`, `src/authoring/RuntimeView.cpp`,
`tests/test_authoring_inspection.cpp`.
Existing: World header/implementation, Runtime header/frame/effects internals,
runner binding files, session/types, their focused tests, CMake/consumer/docs.
The two internal registry/coordinator headers may add only the const schema
lookup overload specified above and its regression coverage. Storage changes
and unrelated registry redesign are not allowed.

Exit: a caller can reconstruct permitted current facts and identify which
claims belong only to a prior frame. No generic World dump, raw event-store
export, frame-driving API or remote mutation is added.
