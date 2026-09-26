# Liquid Development Tracking

## Current status

**Solid:** v0.1.0 released; the pre-Liquid hardening is landed on `main`
(`5979e14`, merged in `3e202ef`; follow-up `2aee8a4`). See
[Pre-Liquid Hardening — September 2026](#pre-liquid-hardening--september-2026).
**Liquid documentation:** L0–L6 specified and integrated on `main`.
**Implementation activation:** none. L0–L6 code remains unimplemented, and no
Stage 2 step is active until the owner records its activation below.

On 6 September 2026 the owner authorized a complete documentation review:
specify every retained milestone; allow evidence-backed Solid change proposals;
use Claude implementation, Codex review, and owner-approved advancement.
Preparing these documents was not acceptance of the resulting design or
activation of L0. See the [validation report](docs/LIQUID_DOCUMENTATION_VALIDATION.md).

On 10 September the owner requested branch cleanup and a commit of the finished
documentation work. The semantic-invariance research from `8003ee6` is retained
and uses the same milestone numbering.

Owner decisions recorded 26 September 2026:

1. L0 is not active. No Stage 2 step is active until the owner separately
   activates one.
2. For an owner-activated step, Claude implements and tests the whole allowed
   scope, including substantive `.cpp` work; Codex independently reviews; the
   owner approves advancement and keeps publication/merge authority. This
   replaces the earlier owner-only `.cpp` convention.
3. The revised L0–L6 order and specifications, including L4–L6 and the L6
   dependency pins (Python MCP 2.1.1, nlohmann/json 3.12.0), are accepted as
   specified-but-inactive design.

## Solid foundation

M1–M6 and S0–S7 completed at v0.1.0 on 22 August 2026. Evidence in
[COMPLETE_SOLID.md](COMPLETE_SOLID.md) and [TRACEABILITY.md](docs/TRACEABILITY.md)
is dated, not a permanent assertion that later reviews cannot find defects.

| Baseline | Revision | Meaning |
| --- | --- | --- |
| Refreshed `origin/main`, 10 September | `af5af08befea` | Design-review baseline; ancestor of the documentation branch |
| Unmerged documentation, 10 September | `76f00e414445` | Starting content of the documentation review |
| Committed Solid hardening | `5979e149e042` | Code foundation reviewed by the documentation pass |
| Hardening merge on `main` | `3e202ef` | `fix/pre-liquid-hardening` merged, including `2aee8a4` |
| Blind Core coverage tests | `f3ef421` | 25 contract-level tests; Core line coverage 93.49% (gcovr 8.3); strict suite 30/30 |

The dated design-review evidence is pinned in the validation report; its
post-handoff reconciliation covers `8003ee6`→`f3ef421`. An implementation step
records its own exact base SHA when the owner activates it.

## Pre-Liquid Hardening — September 2026

**Status:** Done (code and documentation corrections landed on
`fix/pre-liquid-hardening`, merged to `main` in `3e202ef`; not a milestone
approval and does not advance L0).

`docs/PRE_LIQUID_REVIEW.md` (5 September 2026, reviewed revision `76f00e4`)
audited Solid before L0 implementation and reproduced defects that the existing
strict suite did not cover. Each finding below has a regression that fails on
the reviewed code and passes at the closing commit.

| Finding | Severity | Closing commit | Regression |
|---|---|---|---|
| B1 Lua lifecycle argument allocation could abort the host | High | `7d3ca72` | `test_lua_lifecycle`: "lifecycle change arguments exhausting Lua memory yield a bounded result", "lifecycle argument exhaustion after on_start commits nothing", "frame argument construction never escapes the Lua memory bound" |
| B2 effect bindings retained authority for obsolete targets | High | `d39580a` | `test_runtime_effects`: "rebinding an effect component retires the former target's authority", "converting an external component to internal control drops old-target authority", "removed and recreated components do not inherit stale effect bindings", "stale queued reports for a superseded target do not project" |
| B3 concurrent duplicate dispatch could execute twice after cache eviction | High | `e186aa6` | `test_idempotent_dispatcher`: "overlapping duplicate waiters consume the leader outcome after cache eviction", "duplicate waiters validate the shared leader outcome against their own command" |
| B4 completed removals vanished from evidence when callbacks threw | High | `db276ce` | `test_world`: "throwing behavior removal callbacks still record the completed tombstone", "throwing component removal callbacks still record the removed component"; `test_runtime_effects`: "removal tombstones survive throwing callbacks into replayed evidence", "removal tombstones from throwing callbacks drain on the next frame" |
| B5 effects failures left `last_frame_log()` stale | Medium | `ee10106` | `test_runtime_effects`: "effects-path frame failures publish the failed frame log" |
| B6 Solid Scope accepted blank brightness as zero | Medium | `d4f4a9c` | `visualizer_selftest` (parser table in `runParserSelfTest`) |
| C1 event appends relocated history on every append | Perf | `759396a` | `test_event_store`: "single-record appends keep order and count across thousands of records"; local Debug probe 2k/4k/8k appends: 0.50/1.97/7.93 s before, 0.015/0.012/0.022 s after |
| C3 duplicated Lua execution-evidence construction | Simplification | `35e5af4` | `test_lua_behavior`: "execute and execute_lifecycle emit byte-identical execution evidence" |
| C4 capability cache outlived destroyed behaviors | Lifetime | `35e5af4` | `test_lua_behavior`: "capability cache stays bounded under behavior churn in one world" |
| C2 checkpoint retention embeds historical growth | Documented limitation | `ba8269c` | `test_replay`: "checkpoint payloads embed retained history until the value node limit"; `docs/EVENT_FORMAT_V1.md` known-limitation note |
| D1 retention failure promise overstated | Docs | `ba8269c` | `docs/EVENT_FORMAT_V1.md` commit-boundary wording matches `test_event_store` post-replacement case |
| D2 / product scope | Docs | `ba8269c` | `COMPLETE_SOLID.md` dated status/verdict; `PRODUCT.md` heading scoped to Solid Scope |

Public API additions: `World::component_exists(ComponentTarget)` (non-throwing
liveness query), `LuaBehaviorRunner::cached_capability_entries()` (diagnostic),
and `IdempotentDispatcher::waiting_duplicate_dispatches()` (diagnostic count
of calls parked on an in-flight duplicate). No new targets, folders, dependencies, or Event Format v1 changes.

Notes carried forward:

- C2 is a finite-session limitation, not corrupted evidence. Resolve bounded
  current state versus historical evidence as a separate checkpoint design
  decision before long-running operation; keep it out of L0.
- C5: new L0 fixtures should use public `register_component<T>(name, version,
  ComponentCodec<T>)` with the Lua codec, not the legacy codec-less overload
  that test targets enable privately; the pre-Liquid regressions already follow
  this.
- The B3 regression holds the leader and evictor in the adapter, waits until
  every duplicate is parked (`waiting_duplicate_dispatches()`), then completes
  leader and evictor back to back and repeats the scenario. Parking is forced
  by synchronization, so a second adapter call can only be the eviction defect;
  the wake-up versus eviction order is internal, so the fix is asserted
  schedule-independent across repetitions (the reviewed code fails within two
  iterations). ThreadSanitizer coverage relies on the CI `tsan`
  job (Clang is not installed on the development machine).

Verification at the closing revision: strict Debug GCC build with
`LIQUID_ENABLE_STRICT_WARNINGS=ON` and `LIQUID_WARNINGS_AS_ERRORS=ON`, full
`ctest`, and the ASan/UBSan subset for Lua behavior/lifecycle, Runtime/effects,
World, event store, replay, and idempotent dispatcher (results recorded in the
closure note of `docs/PRE_LIQUID_REVIEW.md`).

Follow-up verification found that a removed component still reserved its effect
target until another frame ran. The follow-up to `5979e14` (committed as
`2aee8a4`) reuses
the current-binding lookup before checking target uniqueness, retiring the dead
entry while preserving rejection of duplicate live bindings. The regression
"recreated components can immediately reclaim their effect target" failed on
the pre-fix code with `effect route and target are already bound`, then passed
all six assertions after the fix, including observation projection and live
target conflict rejection. The full strict Debug and ASan/UBSan suites each
passed 30/30. See `docs/PRE_LIQUID_VERIFICATION.md` for the planning-document
integration and CI gates it listed before L0; the planning-document
integration is now done (see [Current status](#current-status)).

Later on `main`: `f3ef421` added 25 blind contract-level Core coverage tests
written from public headers/docs (Core line coverage 90.1% → 93.49% with gcovr
8.3; strict suite 30/30; independently reviewed by Codex).

## Stage 2 sequence

The [roadmap](docs/LIQUID_STAGE2_PLAN.md) owns rationale; the
[common contract](docs/LIQUID_IMPLEMENTATION_CONTRACT.md) owns shared APIs,
limits and gates; each spec owns its milestone's additions and test steps.

| Milestone | Deliverable | Dependencies | Status |
| --- | --- | --- | --- |
| [L0](docs/LIQUID_L0_IMPLEMENTATION_SPEC.md) | Lua schemas and capability manifests | Reconciled Solid baseline | Specified; inactive |
| [L1](docs/LIQUID_L1_IMPLEMENTATION_SPEC.md) | Host-selected scope and immutable proposals | L0 | Specified; inactive |
| [L2](docs/LIQUID_L2_IMPLEMENTATION_SPEC.md) | Isolated lifecycle evaluation | L1 | Specified; inactive |
| [L3](docs/LIQUID_L3_IMPLEMENTATION_SPEC.md) | Scoped current truth and frame evidence | L2 | Specified; inactive |
| [L4](docs/LIQUID_L4_IMPLEMENTATION_SPEC.md) | Bounded session journal | L3 | Specified; inactive |
| [L5](docs/LIQUID_L5_IMPLEMENTATION_SPEC.md) | Approval, activation, replacement and stop | L4 | Specified; inactive |
| [L6](docs/LIQUID_L6_IMPLEMENTATION_SPEC.md) | Optional local MCP adapter | L5 | Specified; inactive |

The old provisional L4=MCP and L6=evidence IDs are superseded. Evidence now
precedes operations; transport follows the complete semantic lifecycle.

## Activation and completion records

Record the step ID, exact base SHA, specification revision, allowed-file subset,
prerequisites, and owner activation before implementation. Claude implements
and tests, Codex reviews, then the owner approves progression.

Completion records contain focused red/green evidence, strict-suite commands
and results, review findings and closure, relevant sanitizer/package evidence,
and the owner decision. Never pre-fill approvals, test totals or remote CI.
No step has been activated, and no implementation steps have completion
records yet.

## Liquid Layer

Future application work owns model routing, user/context meaning, privacy and
consent, real hardware and human studies. Stage 2 proves deterministic
mechanics and a local transport boundary, not clinical benefit, remote-service
security, or indefinitely durable adaptive history.
