# Complete Solid

## Status

**Audit status:** headless implementation audit complete on 13 August 2026; S6 and remote release gates remain.

**Implementation status:** M1-M6 and finalization stages S0-S5 are complete locally. The repository now implements the resolved-effect, command, feedback, authoritative observed-state, durable replay, simulation, and package contracts. Solid is not final until S6 and the remote S7 release gates close.

This document is the accepted Solid completion audit and release gate. On 12 August 2026 the owner approved the full 0.1.0 framework completion contract and the S0-S7 implementation order. M1-M6 remain complete foundations; Stage 2 Liquid work is deferred until S7 closes.

Headless implementation must follow the `main` workflow in `AGENTS.md`. Visualization-only evidence remains on `experiment/stage2` and consumes the headless core by forward merge.

## Completion Definition

Solid is complete when it can deterministically accept validated proposals, select a desired effect, dispatch that effect through a bounded port, consume an authoritative result on the Runtime-owning thread, and update observed state without mistaking desire for physical truth.

The completed M1-M6 path is:

```text
trusted C++ or Lua behavior
    -> validated immutable intent proposal
    -> deterministic lifetime and conflict resolution
    -> selected intent handle
    -> inspectable frame result
```

The completion path that was missing when this audit was accepted, and is now
implemented by S1-S4, is:

```text
selected intent
    -> resolved effect
    -> lifecycle-unique command
    -> simulation or device port
    -> applied / rejected / failed / pending result
    -> bounded feedback ingress
    -> deterministic validation on the Runtime-owning thread
    -> authoritative observed component state
```

Intent resolution and effect application must remain separate. A selected value is a desire; it is not proof that a simulator or physical device applied that value.

## Audit Scope and Method

The pass covered all 68 tracked files on `experiment/stage2`, including:

- 13 public engine headers and all core implementations under `include/` and `src/`;
- every core, Lua, simulation, stress, bridge, and integration test;
- the M6 CLI, shared scenario, deterministic trace executable, loopback bridge, and browser instrument;
- all tracked project documentation and the complete CMake configuration;
- the branch diff against `main`, confirming that Solid Scope changes no `include/` or `src/` file and does not introduce a second `Runtime`.

The review traced ownership, component and behavior lifecycle, intent creation and cleanup, system membership and dispatch, Lua authority and rollback, frame failure, M6 re-execution, trace production, bridge validation, and browser presentation. Static checks also covered unfinished markers, exception boundaries, unsafe casts/allocation patterns, header self-containment, build variants, and sanitizer execution.

## Architecture Confirmed

```text
Runtime
  owns World
    owns WorldState
      BehaviorRegistry
      ComponentRegistry
      IntentRegistry
      SystemRegistry
      behavior signatures and access revisions
    uses Coordinator for cross-registry consistency

Frame
  begin
  -> expire
  -> systems in registration order
  -> expire intents created already-expired in this frame
  -> resolve requested component targets
  -> end
```

- `Runtime` is the only frame-phase driver and becomes fail-stop when an exception escapes an incomplete frame.
- `World` is the public state boundary; topology is frozen during system dispatch.
- `Coordinator` owns cross-manager validation, cleanup sequencing, behavior signatures, and system membership derivation.
- Lua receives fresh typed snapshots and host-bound proposal capabilities. It never receives `World`, registries, raw slots, component pointers, or owner selection.
- Lua proposals are buffered and committed only after successful execution. A failed execution removes only the intents created by that execution.
- `SimulationScenario` exercises the real `Runtime` and Lua path. The CLI and NDJSON trace are projections of that path.
- Solid Scope replays recorded trace presentation; it does not perform source-line stepping or run another engine.

## Verification Baseline

The following checks passed during this audit:

| Verification | Result |
|---|---|
| Strict GCC build with `-Wall -Wextra -Wpedantic -Wconversion -Werror` | Passed |
| Strict CTest suite | 18/18 passed |
| Release strict build | Passed |
| Release CTest with assertions active | 18/18 passed |
| AddressSanitizer + UndefinedBehaviorSanitizer strict build | Passed |
| Sanitizer CTest suite | 18/18 passed |
| Public-header isolated compilation | 13/13 passed |
| Diff from `main` under `include/` and `src/` | Empty |
| Whitespace/error residue check | Passed |

This table is the historical pre-finalization baseline. It did not prove the
effect/feedback path or durable replay; the current S0-S5 evidence in
`docs/TRACEABILITY.md` does.

## Completion Matrix

### Implemented and accepted through M6

| Responsibility | Evidence | Classification |
|---|---|---|
| World-local recyclable behavior, intent, component-type, and component-slot handles | Registries, storage, recycling and exhaustion tests | Implemented |
| Typed named component storage with explicit liveness and immediate value destruction | `ComponentStorage`, `ComponentRegistry`, stress tests | Implemented |
| Shared component access with per-behavior read/write modes | `World`/`Coordinator` access APIs and cleanup tests | Implemented |
| Coordinator-owned signatures and deterministic system membership | System and world regressions, including empty signatures | Implemented |
| Immutable-by-normal-API typed intent records with owner and target indexes | `IntentRegistry` and resolution tests | Implemented with payload caveat below |
| Cleanup when owner, target, or write permission disappears | World and stress regressions | Implemented |
| Persistent and until-time lifetime with deterministic expiration | Expiration, resolution, Runtime, and stress tests | Implemented |
| Priority then higher-`IntentId` selection | Resolution and recycling tests | Implemented by explicit contract |
| Nondecreasing explicit time and registration-ordered frame execution | Runtime tests | Implemented |
| Fail-stop Runtime after an escaped frame exception | Runtime tests | Implemented; evidence detail remains limited |
| Controlled Lua 5.4.8 capability boundary, limits, diagnostics, and rollback | Lua runner and adversarial tests | Implemented |
| Hardware-free deterministic CLI with bounded failures | M6 CLI, process, scenario, and stress tests | Implemented |
| Byte-identical re-execution for identical M6 inputs | Fresh-process golden regression repeated 20 times | Implemented |
| Deterministic NDJSON observation and loopback-only browser experiment | Trace, bridge, and integration tests | Implemented experiment; polish findings remain |

M6 “replay” means deterministic re-execution of identical inputs and byte comparison. It is not serialized state restoration, persisted event replay, or command/result replay. Likewise, the Solid Scope NDJSON stream is deterministic observation, not an authoritative durable event log.

### Accepted completion findings (closed by S1-S4)

| Finding | Why it matters | Required outcome |
|---|---|---|
| Resolved-effect representation | Resolution currently returns only `ComponentName -> IntentId` | Produce a typed, inspectable effect without mutating observed state |
| Lifecycle-unique command identity | `IntentId` is recyclable and cannot safely correlate delayed results | Use a non-recyclable session command sequence or equivalent identity independent of runtime handles |
| Typed command encoding boundary | Generic component values cannot cross a type-erased adapter boundary safely by inference | Register trusted codecs/schemas explicitly and reject unknown or mismatched values |
| Dispatch port | Adapters must not receive `World`, registries, slots, or component pointers | Expose the smallest command-only interface shared by simulation and future physical adapters |
| Bounded result ingress | External callbacks cannot mutate the single-thread-confined world | Queue bounded reports and consume them only during a defined Runtime phase |
| Result state machine and validation | Duplicate, stale, mismatched, delayed, and out-of-order results can create false state | Define pending/applied/rejected/failed/timeout states and idempotent validation rules |
| Authoritative observed-state application | Selected intent values currently leave the component unchanged, correctly but incompletely | Update observed state only from an accepted applied report, including device normalization when reported |
| Command/result provenance | Current frame logs contain selections but no command/result correlation | Record frame, command, source intent evidence, result, diagnostics, and state transition explicitly |
| Deterministic simulator | Hardware-free testing must exercise the production-shaped contract | Implement latency, normalization, rejection, failure, missing, duplicate, stale, and out-of-order reports without becoming a second Runtime |
| Durable serialization/replay contract | Re-execution alone cannot restore or audit asynchronous outcomes | Define serializable records and deterministic replay/checkpoint expectations for accepted evidence |

### Accepted core-hardening findings (closed by S1-S3)

The numbered text below preserves the original audit rationale. Its future-tense
requirements are historical; the current closure evidence is tracked in
`docs/TRACEABILITY.md`.

1. **Runtime handles are preconditioned, not historical identities.** `BehaviorId`, `IntentId`, `ComponentSlotId`, and `ComponentType<T>` have no generation or world identity and are deliberately recycled. A stale handle can alias a new live object; a type handle from another world can match the same numeric type and C++ type. Existing stale resolution maps are rejected, and Lua capability caches use lifecycle-unique access revisions, but the general public handles remain caller-disciplined. Solid completion must either preserve this as an explicit trusted-call precondition or introduce stronger handles where stale input can cross a boundary. Command correlation must never use these recyclable IDs alone.

2. **The trusted mutable C++ escape hatch must remain confined.** Public `World::get_component_named` and `World::resolve_component` can return mutable pointers without a `BehaviorId`. This is acceptable for a trusted host effect-application boundary, but it is not behavior authority and must never be exposed to scripts or adapters. The completion design must name the only code allowed to use it, or replace it with a narrower observed-state command.

3. **Intent immutability is shallow for arbitrary C++ payloads.** The registry exposes records as `const`, but an unconstrained component payload can contain pointers, shared owners, references, or other aliasing state whose semantics remain externally mutable. The project currently uses value-semantic structs. Before generic effect codecs are accepted, component/intent authoring must require deep value semantics or enforce an equivalent type/codec constraint and add a regression.

4. **Failed-frame evidence is not sufficient for replay-grade diagnosis.** `FrameLog` records phase, message, and the count of systems completed, but not the failing system identity or partial intents/component writes. That is adequate for M4 fail-stop behavior; it is insufficient for the completion requirement that all replay-relevant transitions be explicit.

5. **Signature-change callback failure needs a World-level regression.** Direct registry callback consistency is tested, but changing a system signature through `World` across multiple behavior memberships lacks a throwing-callback test that freezes the documented commit-then-rethrow guarantee.

6. **Frame-number exhaustion is unchecked.** `FrameNumber` is `uint64_t` and successful frames increment without an overflow guard. This is practically remote, but completion should reject exhaustion rather than repeat historical frame identity and bypass the nondecreasing-time guard.

These findings do not invalidate M1-M6. They define the hardening needed before the larger standalone-runtime claim is made.

## Solid Scope Experiment Findings

Solid Scope correctly demonstrates the present boundary: selected values are `[70, 30]` while authoritative actual values remain `[10, 10]`. That is evidence of the missing application loop, not a visualizer defect. The experiment also preserves Solid core semantics and uses a loopback bind allowlist, fixed static-file allowlist, Host/Origin/token checks, request and trace bounds, timeout enforcement, terminal/exit correlation, and direct-child cleanup.

The following experiment issues remain before calling Solid Scope polished completion evidence:

| Priority | Finding | Required treatment |
|---|---|---|
| Medium | Malformed `Origin` metadata and extremely long digit-only `Content-Length` can raise uncaught `ValueError`, close the connection, and return no bounded HTTP response | Convert parser failures into deterministic bounded 4xx JSON and add raw-socket regressions |
| Medium | A trace process can spawn a descendant, exit, and leave that descendant alive because cleanup returns when the direct child has already exited | Always clean the created process group safely; add a grandchild regression |
| Medium | Rendering up to 1,000 accepted frames repeatedly rebuilds the full frame table, producing quadratic DOM work and very long minimum playback | Update rows incrementally or batch/catch up; align the accepted bound with a measured usable presentation contract |
| Accepted support boundary | No automated external browser harness executes `app.js` | Solid Scope is an owner-operated development instrument, not a user-facing product. Keep its dependency-free browser self-test and automated bridge/integration coverage; the owner manually verifies streaming, controls, malformed data, reconnect/error states, and 1,000-frame responsiveness. Do not add Playwright or a Node package dependency. |
| Medium | The bridge uses POSIX selectors, nonblocking pipes, process groups, and signals but CMake registers its tests unconditionally | Implement a Windows path or declare/gate the experiment as UNIX-only |
| Low | A 150 ms timeout regression is startup-scheduling-sensitive and has failed once under the sanitizer build | Synchronize on the first child event or use a non-racy deadline |
| Low | The bridge validates the trace envelope and terminal correlation but not required fields for every named v1 event | Define and validate per-event schemas and safe integer ranges at one boundary |

The first two experiment findings are resource/safety defects and should precede visual polish. Browser automation is explicitly outside the support boundary; performance, platform, and schema evidence still must not remain implicit.

## Documentation and Operational Polish

The repository documentation contains historical snapshots that are useful but no longer read as historical:

- `CURRENT_STATE_EVALUATION.md` still says M6 is current, reports 12/12 tests, and contains pre-M6 line references. Add a prominent superseded/historical banner pointing here rather than rewriting its dated evidence.
- `M6_TEST_BASE.md` still says it is under final owner acceptance even though M6 is done.
- `Liquid_Concepts_and_Architecture.md` says M1-M5 complete and M6 current; it also calls a speculative broad folder tree “current intended” and lists some already-settled decisions as open.
- `DEVELOPMENT_TRACKING.md` and `AGENTS.md` call Stage 1 complete and Stage 2 planning current, which conflicts with the completion definition in this audit.
- the minimal repository trees in operational documents omit the current app scenario, trace, visualizer, and focused tests or fail to label themselves as the headless baseline.
- `ARTICLE_NOTES.md` stops its implemented-stage narrative around early Solid work; it should be labeled historical or extended through M6.

Documentation authority after owner approval should be:

1. `AGENTS.md` for operational scope and branch ownership;
2. `DEVELOPMENT_TRACKING.md` for approved milestone status and order;
3. this document for the accepted Solid completion audit and gates;
4. `Liquid_Concepts_and_Architecture.md` for canonical architecture and future direction;
5. dated evaluations and article notes as historical/non-normative evidence.

Release-facing polish candidates also remain: a top-level README with supported build/run/test instructions, CI for strict/Release/sanitizer gates, a license if the engine remains intended for reuse, and a milestone-to-test traceability table. Namespace normalization, broad catch cleanup, test-framework migration, and small expiration-overload duplication are lower-priority debt and should not be mixed into the effect contract.

## Decisions Resolved

The approved contract resolves the audit questions: Solid includes the full command/feedback/observed-state loop; queued feedback is applied at the next frame's opening feedback phase; desired, outstanding, reported, and observed state remain distinct; session, command, record, intent, and world identities have explicit non-aliasing sequence rules; component and intent data use bounded canonical `Value` snapshots behind trusted codecs; deterministic retry and reconciliation belong to the core contract; replay includes generic projection and host-assisted re-execution; Solid Scope is Unix-only and requires real-browser coverage; all release-polish items named in S5-S7 are gates.

## Approved Ordered Completion Milestones

### S0 — Contract and branch alignment

Reconcile operational documents and freeze the public API, event format, threading, adapter/retry, replay, compatibility, security, support, and traceability contracts.

### S1 — Public core hardening

Normalize the public surface; add world-bound generational handles, monotonic intent sequence ordering, bounded values/codecs, immutable snapshots, transactional mutation, identity/evidence/overflow/thread regressions, and Catch2 v3.

### S2 — Durable records and replay

Implement canonical encoding, memory/file stores, locking, durability, recovery, checkpoints, retention, projection, verification, golden fixtures, fault injection, and decoder fuzzing.

### S3 — Effects and feedback

Implement effects, adapter routing, commands, reports, bounded feedback, timing modes, supersession, retries, timeout, late-report ordering, durable outbox, reconciliation, and idempotent dispatch.

### S4 — Simulator and complete Solid scenarios

Exercise the real adapter interface across canonical immediate/deferred paths and every accepted failure mode, then prove projection and host-assisted verification.

### S5 — Framework packaging

Ship static Core/Lua/Simulation targets, vendored Lua 5.4.8, source/install consumers, release documentation, CI, sanitizers, coverage, and fuzz gates.

### S6 — Solid Scope completion

Forward-merge headless work and close the accepted bridge, process, schema, playback, platform-gating, browser, and performance findings without adding a second Runtime.

### S7 — Final audit and release candidate

Repeat regression-first closure until no critical, high, or medium findings remain; document or fix lows; complete the traceability matrix and release gates.

## Final Acceptance Gate

Solid may be declared complete only when all owner-accepted items below are true:

- M1-M6 regressions remain green.
- Selected desire, dispatched command, reported result, and observed state are distinct in types and evidence.
- Adapters have no world mutation authority.
- External reports are bounded and applied only on the Runtime-owning thread.
- Duplicate, stale, recycled, cross-world, mismatched, delayed, missing, failed, rejected, timed-out, and out-of-order cases cannot create false observed state.
- Command identity remains unique for the required session/history horizon and does not depend on recyclable runtime handles.
- Intent/effect payloads meet the accepted value-semantics and codec contract.
- Failed frames and external results provide the accepted replay-grade diagnostics.
- The simulator uses the same semantic port as future physical adapters.
- Re-execution and recorded-result replay are both named and tested according to their actual guarantees.
- Strict, Release, and sanitizer builds pass the full suite.
- Solid Scope issues selected as acceptance gates are closed or explicitly excluded by a documented platform/support decision.
- Operational, tracking, architecture, test-base, and historical documents agree on the final status.

## Out of Scope for Solid Completion

Unless the owner explicitly changes the boundary, the following remain outside this plan:

- real MQTT or production smart-home hardware;
- LLM integration, generation, repair, or adaptive proposal policy;
- voice and wake-word pipelines;
- biosignals, physiological inference, ML, participant studies, or clinical claims;
- final Liquid Layer application behavior;
- a general event subsystem beyond the smallest explicitly approved feedback ingress;
- until-frame or script-defined intent lifetimes unless the approved effect contract requires them.

## Audit Verdict

M1-M6 are internally coherent, comprehensively tested for their stated contracts, and clean under the current strict, Release, and sanitizer gates. Solid Scope correctly observes that core without changing it. No critical defect was found that reopens a completed M1-M6 milestone.

The headless audit found no unresolved critical, high, or medium finding after the S1-S5 regression loop. The remaining work is operational: land and forward-merge the canonical headless implementation, complete the Unix-only Solid Scope S6 contract without semantic divergence, and obtain the supported-platform CI evidence required by S7. The support boundaries documented for local filesystems, private/no-license use, and optional components remain intentional v0.1 limits.
