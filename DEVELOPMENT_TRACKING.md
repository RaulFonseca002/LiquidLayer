# DEVELOPMENT_TRACKING.md — Liquid Project Tracking

This file tracks the practical development path for the `liquid` engine.

The goal is to keep each active milestone small, testable, and explicit. Detailed Solid v0.1 completion evidence lives in `COMPLETE_SOLID.md` and `docs/TRACEABILITY.md`; Stage 2 architecture/roadmap lives in `docs/LIQUID_STAGE2_PLAN.md`; L0 implementation semantics live in `docs/LIQUID_L0_IMPLEMENTATION_SPEC.md`.

---

## Project Layers

### Solid

The deterministic foundation of Liquid.

**Status:** Complete and released at v0.1.0.

Solid owns behavior identity/access, immutable intents/lifetime/resolution, frame execution, effects/feedback/observations, bounded Lua lifecycle execution, durable evidence/replay, deterministic simulation, and packaging.

### Liquid

The reusable model-facing control and authoring layer over Solid.

**Status:** Current development stage.

Liquid lets external models/agents understand, author, validate, inspect, and eventually perform bounded operations over Solid behavior without direct Runtime authority. It does not choose the model/provider/agent runtime.

### Liquid Layer

The final adaptive smart-environment application/research project.

**Status:** Future application stage; current architectural acceptance target.

Liquid Layer owns user/environment context, sensors/integrations, neurodivergent-support policy, when inference runs, local/hosted selection, direct/agent selection, conversation/memory, real smart-home integration, privacy, and user experience.

---

## Current Development Policy

Stage 1 is frozen at Solid v0.1.0. Stage 2 may extend the framework only through approved bounded milestones and must not weaken Solid contracts.

Current milestone: **L0 — Model-Facing Lua Capability Contract**.

```text
Liquid Layer
    application context / AI routing / policy
        │
        ▼
Liquid
    discover / author / validate / evaluate / observe / bounded operation
        │
        ▼
Solid
    deterministic execution / authority / evidence
```

Direct versus agentic reasoning and local versus hosted deployment are independent application choices. MCP is future transport, not the internal model.

---

# Stage 1 — Solid

**Status:** Complete — v0.1.0, 22 August 2026.

Detailed implementation/release history is in:

- `COMPLETE_SOLID.md`;
- `docs/TRACEABILITY.md`;
- `docs/PUBLIC_API.md`;
- `docs/LIFECYCLE_SCRIPTING.md`;
- `docs/THREADING.md`;
- `docs/ADAPTER_CONTRACT.md`;
- `docs/EVENT_FORMAT_V1.md`.

Completed: M1-M6 and S0-S7.

Frozen facts relevant to Liquid:

- `Runtime` alone drives frames; `World` is the public state boundary.
- Internal registries/Coordinator/storage are not model API.
- `World`/`Runtime` are owner-thread confined.
- runtime handles are world-bound generational handles.
- intents are immutable; current lifetimes are `Persistent`/`UntilTime`; explicit cancellation destroys.
- losing selection does not destroy a live intent.
- selected desire, command, report, and observed external truth are separate.
- Lua runs in a fresh bounded capability VM and commits named proposals/cancellations/watches transactionally.
- `LuaComponentCodec<T>::encode` and `decode` are independent executable directions and need not have symmetric Lua value shapes.

---

# Stage 2 — Liquid

## L0 — Model-Facing Lua Capability Contract

**Status:** Current — approved for implementation.

### Goal

Make the existing Lua boundary machine-readable for external authors without any model/provider/MCP integration or generated-behavior activation.

```text
trusted Lua binding + trusted read/write model metadata
                    │
                    ▼
prepared behavior current World permission
                    │
                    ▼
copied readable snapshots
                    │
                    ▼
immutable bounded capability manifest
```

### Required concepts

- bounded `LuaValueSchema` mapping exact current `LuaValue` kinds;
- separate `readSchema` (`encode` output) and `writeSchema` (`decode` input), plus symmetric helper for common codecs;
- bounded trusted binding/field descriptions;
- metadata attached beside the existing Lua binding, not in a parallel registry;
- immutable `LuaCapabilityManifest` built through `LuaBehaviorRunner`;
- exact host-generated Lua access expressions;
- current `World` permission projection;
- copied readable values validated against `readSchema`;
- representative `writeSchema` values checked with the real codec decoder;
- captured monotonic `now_ms` and a stable Lua authoring-contract/version marker;
- explicit schema/manifest/description/diagnostic limits;
- current Integer/Number and empty-array Lua construction semantics represented accurately.

Exact schema kinds:

- Boolean = `bool`;
- Integer = `std::int64_t`;
- Number = finite `double`, with no implicit Integer coercion;
- String = bounded `std::string`, optional enum;
- Array = `LuaValue::Array`, one item schema/count bounds;
- Object = `LuaValue::Table`, required/optional named fields, unknown fields rejected by default.

Do not add null, bytes, numeric coercion, unions/composition, `$ref`, regex, arbitrary JSON Schema, or provider-specific keywords without a real codec requirement.

For a fully model-described binding:

| `World` permission | manifest read side | manifest write side |
| --- | --- | --- |
| Read | `readSchema` + copied value | absent |
| Write | absent | `writeSchema` |
| ReadWrite | `readSchema` + copied value | `writeSchema` |

Schema/provider metadata never grants authority. Actual codec, `World`, host closures, and intent transaction remain final.

### L0 files

```text
include/liquid/scripting/LuaValueSchema.hpp
include/liquid/scripting/LuaCapabilityManifest.hpp
include/liquid/scripting/LuaBehaviorRunner.hpp
src/scripting/LuaValueSchema.cpp
src/scripting/LuaCapabilityManifest.cpp
src/scripting/LuaBehaviorRunner.cpp
tests/test_lua_schema.cpp
tests/test_lua_manifest.cpp
CMakeLists.txt
AGENTS.md
DEVELOPMENT_TRACKING.md
Liquid_Concepts_and_Architecture.md
README.md
docs/LIQUID_STAGE2_PLAN.md
docs/LIQUID_L0_IMPLEMENTATION_SPEC.md
docs/LIFECYCLE_SCRIPTING.md  # only if executable public Lua contract changes
```

Keep L0 inside existing `scripting/` / `Liquid::Lua`. No speculative adaptive/provider/agent/MCP folders or targets.

### Implementation order

1. Failing exact-schema construction/value tests.
2. Minimal public schema header.
3. Owner implementation of validation/limits.
4. Symmetric and intentionally asymmetric codec fixtures.
5. Manifest permission/copy/path/time/version tests.
6. Minimal manifest API + additive model metadata on existing runner binding.
7. Owner implementation of manifest construction through runner/current World permissions.
8. Revocation/removal/path/empty-array/bounds/mismatch cases.
9. CMake/public docs for actual implemented surface.
10. Strict full regression and relevant sanitizer/consumer checks.

Owner writes substantive `.cpp` logic unless explicitly delegating it; agents focus on headers/tests/CMake/boilerplate/docs.

### Success evidence

- every exact schema kind/bound and Integer/Number non-coercion;
- symmetric `Light{brightness 0..100}` + asymmetric codec fixtures;
- readable snapshots validated against `readSchema`;
- write fixtures exercised through actual decoder;
- exact permission projection;
- access revoke/remove rebuild;
- unusual names generate safe deterministic access expressions resolving in real Lua;
- immutable copied bounded manifest with `now_ms`/contract version;
- empty-array limitation remains truthful;
- old schema-less exposure remains source-compatible/executable but absent from model discovery;
- model metadata cannot enlarge Lua/World authority;
- full strict suite remains green.

### Out of scope

No provider/model call, Hermes, MCP, prompt repair, behavior activation, generic runtime inspection, remote mutation, new DSL/IR, semantic-trigger abstraction, hardware/MQTT/voice/biosignals, Liquid Layer policy, or Solid Event Format v1 changes.

---

## L1 — Behavior Proposal and Prospective Authoring Scope

**Status:** Provisional — re-evaluate after L0.

**Question:** Can an external author propose a new behavior without creating a broadly privileged live behavior merely to discover capabilities?

Likely scope:

- immutable exact Lua `BehaviorProposal` + bounded review metadata;
- trusted prospective authoring scope selected by host/application;
- L0-style manifest for that scope;
- source/contract/capability validation;
- stale-scope detection;
- explicit new behavior versus approved-source revision distinction;
- no model-selected arbitrary permissions.

L1 must decide from L0 evidence whether prospective scope is represented by a prepared non-executing Solid behavior or separate host-owned scope data.

---

## L2 — Deterministic Proposal Evaluation

**Status:** Provisional — re-evaluate after L1.

**Question:** Can a candidate proposal be exercised through the real Solid path before activation?

Likely scope:

- isolated prepared `World`/`Runtime`;
- real `LuaLifecycleSystem`/runner;
- real intent lifetime/resolution;
- real `InMemoryAdapter`;
- deterministic scenario inputs;
- evaluation result separating proposals, selections, commands, reports, observations, authoritative final state;
- bounded diagnostics suitable for an external repair loop.

The light-specific current `SimulationScenario` is not assumed generic; extract only what tests need.

L2 completes the first end-to-end authoring/evaluation slice without an LLM provider.

---

## L3 — Read-Only Liquid Runtime View

**Status:** Provisional — re-evaluate after L2.

**Question:** Can a caller reconstruct relevant current Solid truth without registry access, shadow state, or conversation memory?

Likely scope:

- owner-thread immutable snapshots;
- model/API-safe behavior references;
- live owned intents with name/value/target/priority/lifetime;
- selected/not-selected + competitor/owner;
- authoritative observed external state;
- bounded command/report/evidence facts;
- explicit snapshot/frame consistency.

Known problems:

- generic type/component naming from `ComponentTarget` without registry exposure;
- external target -> observed state without private Runtime binding exposure;
- one consistent capture of live intents + selected resolution;
- world-local generational handles must not masquerade as global historical IDs.

Required acceptance: FocusSupport persistent ON loses to FollowUser OFF, remains live, and can win again; one captured view represents all facts correctly.

---

## L4 — MCP Adapter

**Status:** Provisional — re-evaluate after L3.

**Question:** Can agent runtimes consume the same Liquid semantics without gaining authority?

Likely initial tools: focused discovery, proposal validation/evaluation, read-only inspection. No unrestricted mutators.

Current protocol direction (28 Aug 2026):

- stable MCP is `2026-07-28`, stateless core;
- do not build new work on deprecated Sampling/Roots/Logging;
- model invocation is client/application-owned;
- MCP schemas render Liquid contracts;
- tools stay few/focused/context-scoped;
- annotations/descriptions are not authorization;
- requests/results validated locally;
- owner-thread confinement preserved with copied snapshots/marshalled mutation requests;
- prefer local/owner-controlled deployment before remote OAuth unless needed;
- remote auth, if introduced, must validate intended issuer/audience and never pass inbound client tokens through.

Current official Tier 1 MCP SDKs: TypeScript, Python, Go, C#; no official C++ SDK listed. Re-evaluate at L4.

Hermes is one integration proof, not contract owner; also test another client/conformance path.

---

## L5 — Approval, Activation, Revision, and Bounded Operations

**Status:** Provisional — re-evaluate after L4.

**Question:** Can reviewed generated behavior become live/revised/stopped without stale approval, source substitution, or authority expansion?

Likely scope:

- approval bound to exact source/proposal hash;
- scope/access revision and evaluation evidence;
- install exact approved source;
- explicit revision workflow;
- scenario-justified ownership-aware stop/cancel/remove;
- no arbitrary world/intent mutation.

Critical: define the fate of persistent intents from the previous approved script revision. Replacing `LuaBehaviorScript.source/revision` alone is insufficient.

---

## L6 — Liquid Proposal Evidence and Change Surface

**Status:** Provisional — re-evaluate after L5.

Likely scope:

- separate proposal/evaluation/approval/revision evidence;
- correlation to Solid runtime evidence;
- optional application-supplied provider/model metadata;
- privacy-aware record of shared context/capabilities;
- bounded change feed only if Liquid Layer proves it necessary.

Liquid evidence answers why adaptive artifacts changed. Solid evidence answers what deterministic execution did/observed.

---

# Stage 3 — Liquid Layer

**Status:** Future.

Expected: neurodivergent-support scenarios, application-specific inference/revalidation triggers, local/hosted routing, optional Hermes/other agents, consent/privacy/data export, real devices, simulation/user studies.

Whenever possible, semantic reasoning should become deterministic approved Lua/intent policy so Solid continues correctly while all models are offline.

---

## Pre-Liquid Hardening — September 2026

**Status:** Done (code and documentation corrections landed on
`fix/pre-liquid-hardening`; not a milestone approval and does not advance L0).

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
target until another frame ran. The working-tree follow-up to `5979e14` reuses
the current-binding lookup before checking target uniqueness, retiring the dead
entry while preserving rejection of duplicate live bindings. The regression
"recreated components can immediately reclaim their effect target" failed on
the pre-fix code with `effect route and target are already bound`, then passed
all six assertions after the fix, including observation projection and live
target conflict rejection. The full strict Debug and ASan/UBSan suites each
passed 30/30. See `docs/PRE_LIQUID_VERIFICATION.md` for the remaining planning
document integration and CI gates before L0.

---

## Stage 2 Advancement Rule

For each milestone:

1. short-lived branch from current `main`;
2. close one missing capability;
3. identify reused Solid primitives before adding abstractions;
4. define data/header/failure semantics first;
5. write success/stale/permission/bounds/adversarial tests;
6. no provider/network dependency unless that milestone integrates it;
7. preserve owner-thread/deterministic authority;
8. strict regression + relevant sanitizer/consumer checks;
9. record completion evidence;
10. re-evaluate next provisional milestone rather than auto-expanding;
11. update operational/tracking/design docs before widening scope.

A new abstraction must solve a demonstrated requirement current primitives cannot cleanly solve.

---

## Current Notes

- Solid v0.1.0 is frozen foundation.
- L0 is the only implementation-authorized Liquid milestone.
- Lua remains generated executable behavior boundary.
- Liquid does not own model/provider choice.
- MCP waits for stable semantic APIs.
- Hermes remains optional/replaceable.
- Model may be stateless; system is not.
- Losing intent resolution does not kill intent.
- Application semantic triggers/revalidation remain Liquid Layer policy absent repeated proof of a missing engine primitive.
- Owner implements substantive `.cpp` logic unless explicitly delegating.
