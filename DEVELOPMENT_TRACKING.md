# DEVELOPMENT_TRACKING.md — Liquid Project Tracking

This file tracks the practical development path for the `liquid` engine.

The goal is to keep each active milestone small, testable, and explicit. Detailed Solid v0.1 completion evidence lives in `COMPLETE_SOLID.md` and `docs/TRACEABILITY.md`; Stage 2 architecture/rationale lives in `docs/LIQUID_STAGE2_PLAN.md`; L0 implementation semantics live in `docs/LIQUID_L0_IMPLEMENTATION_SPEC.md`.

---

## Project Layers

### Solid

The deterministic foundation of Liquid.

**Status:** Complete and released at v0.1.0.

Solid owns:

- behavior identity and component access;
- immutable intents, lifetime, and deterministic resolution;
- frame execution;
- effects, commands, reports, and authoritative observations;
- bounded Lua lifecycle execution;
- durable evidence/replay;
- deterministic simulation and packaging.

Solid does not choose or run LLM providers and does not contain Liquid Layer application policy.

### Liquid

The reusable model-facing control and authoring layer over Solid.

**Status:** Current development stage.

Liquid exists so an external model or agent can understand, author, validate, inspect, and eventually perform bounded operations over Solid behavior without receiving direct Runtime authority.

Liquid does not choose which model/provider/agent runtime to use. It exposes a stable semantic surface that an application can consume directly or expose through transports such as MCP.

### Liquid Layer

The final adaptive smart-environment application/research project.

**Status:** Future application stage; used now only as an architectural acceptance target.

Liquid Layer owns:

- user/environment context;
- sensors and external integrations;
- neurodivergent-support policy;
- deciding when inference is useful;
- choosing local versus hosted inference;
- choosing direct model calls versus an agent runtime such as Hermes;
- conversation/memory policy;
- real smart-home integration and user-facing behavior.

---

## Current Development Policy

Stage 1 is frozen at Solid v0.1.0. Stage 2 may extend the framework only through owner-approved Liquid milestones and must not weaken Solid contracts to make model integration easier.

Current milestone: **L0 — Model-Facing Lua Capability Contract**.

```text
Liquid Layer
    application context / AI routing / policy
        │
        ▼
Liquid
    discover / author / observe / validate / evaluate / bounded operation
        │
        ▼
Solid
    deterministic execution / authority / evidence
```

Direct inference and agent execution are separate application choices. Local versus hosted inference is another independent choice. Liquid does not own a provider abstraction unless a later reusable engine requirement proves one is needed.

MCP is a future transport over Liquid's semantic API, not Liquid's internal domain model.

---

# Stage 1 — Solid

**Status:** Complete — v0.1.0, 22 August 2026.

Detailed implementation/release history is intentionally not duplicated here. See:

- `COMPLETE_SOLID.md` — accepted completion gates;
- `docs/TRACEABILITY.md` — milestone/test traceability;
- `docs/PUBLIC_API.md` — public framework contract;
- `docs/LIFECYCLE_SCRIPTING.md` — executable Lua lifecycle contract;
- `docs/THREADING.md` — owner-thread contract;
- `docs/ADAPTER_CONTRACT.md` — effects/feedback contract;
- `docs/EVENT_FORMAT_V1.md` — durable evidence format.

Completed implementation stages:

- **M1** — Modified ECS Core;
- **M2** — Intent Lifetime and Expiration;
- **M3** — Intent Resolution;
- **M4** — Minimal Frame Loop;
- **M5** — Lua Behavior Scripting;
- **M6** — Simulation CLI;
- **S0-S7** — Solid v0.1 finalization, hardening, Scope, packaging, and release audit.

Frozen facts especially relevant to Liquid:

- `Runtime` is the sole frame-phase driver.
- `World` is the public state/behavior/component boundary.
- `Coordinator`, registries, `WorldState`, storage internals, raw slots, and mutable pointers are not public/model-facing API.
- `World` and `Runtime` are single-thread-confined.
- Public runtime handles are world-bound generational handles.
- Intent semantic contents are immutable after creation.
- Current intent lifetimes are `Persistent` and `UntilTime`; explicit cancellation destroys the intent.
- A live intent that loses resolution remains alive and may win again later.
- Selected desire, command, report, and authoritative observed external state are distinct facts.
- Lua executes in a fresh capability-bounded VM and may only propose/cancel owner-scoped intents through the host contract.
- Existing Lua lifecycle proposal/cancellation/watch bundles commit transactionally.
- `LuaComponentCodec<T>` has independent `encode` and `decode` directions; Solid does not require their Lua value shapes to be symmetric.

---

# Stage 2 — Liquid

## L0 — Model-Facing Lua Capability Contract

**Status:** Current — approved for implementation.

### Goal

Make the existing Lua behavior boundary machine-readable enough for an external model/agent to author against it without adding a model provider, MCP transport, new runtime authority, or a second behavior language.

```text
trusted Lua binding
    + trusted read/write schema metadata
        │
        ▼
prepared behavior's current World permissions
        │
        ▼
copied readable values
        │
        ▼
immutable bounded Lua capability manifest
        │
        ▼
host-side validation
```

L0 performs no inference and activates no generated behavior.

### Required concepts

- bounded `LuaValueSchema` matching exact current `LuaValue` storage kinds;
- separate trusted `readSchema` and `writeSchema` for model-visible Lua bindings, with a symmetric helper for common codecs;
- optional bounded trusted descriptions sufficient to explain binding/field meaning or units without building an ontology;
- model-facing metadata registered beside the existing executable Lua binding, not in a second capability registry;
- immutable `LuaCapabilityManifest` built through `LuaBehaviorRunner` for one prepared behavior/current access state;
- exact host-generated Lua access expressions;
- explicit projection of current `World` permission;
- copied readable snapshots validated against `readSchema`;
- `writeSchema` values exercised against the real codec decoder in tests;
- current monotonic `now_ms` and a stable Lua authoring-contract/version marker;
- explicit schema/manifest resource limits and deterministic bounded diagnostics;
- current empty-array construction limitation remains visible in the authoring contract.

### Exact V1 schema kinds

- Boolean = `bool`;
- Integer = `std::int64_t`;
- Number = finite `double`;
- String = `std::string` with finite bounds/optional enum;
- Array = `LuaValue::Array` with one item schema/count bounds;
- Object = `LuaValue::Table` with named required/optional fields; unknown fields rejected by default.

Do not add implicit Integer/Number coercion. Do not add null, bytes, unions/composition, `$ref`, recursive schema graphs, regex, arbitrary JSON Schema, or provider-specific keywords until a real codec requires them.

The canonical schema remains a Liquid type. Future adapters may render it to MCP JSON Schema or provider-specific structured-output subsets.

### Authority rules

Schema metadata is descriptive validation, not authority.

- Current `World` permission remains final.
- Host-bound Lua capability closures remain final.
- `LuaComponentCodec<T>::decode` remains final write/proposal validation.
- Transactional commit remains final mutation validation.
- A model-facing description never grants a missing permission.
- Provider-side structured/constrained output never replaces local validation.
- Missing/incorrect model metadata fails closed for discovery and cannot widen executable Lua authority.

For a fully described binding:

| `World` permission | Manifest read side | Manifest write side |
| --- | --- | --- |
| Read | `readSchema` + copied value | absent |
| Write | absent | `writeSchema` |
| ReadWrite | `readSchema` + copied value | `writeSchema` |

### L0 allowed files

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
docs/LIFECYCLE_SCRIPTING.md      # only when public Lua contract changes
```

Keep implementation inside the existing `scripting/` / `Liquid::Lua` boundary. Do not create speculative `adaptive/`, provider, agent, or MCP directories/targets in L0.

### L0 implementation order

1. Draft failing `LuaValueSchema` construction/value tests.
2. Draft the minimal public schema header.
3. Implement schema validation/limits.
4. Add symmetric and intentionally asymmetric codec fixtures.
5. Draft manifest permission/copy/path/time/version tests.
6. Draft the smallest manifest API plus additive model-metadata registration on the existing Lua binding.
7. Implement manifest construction through `LuaBehaviorRunner` using current `World` permissions/snapshots.
8. Add access revocation/removal, exact path escaping, empty-array, bounds, and mismatch cases.
9. Update CMake and scripting contract docs for only the implemented surface.
10. Run strict full regression and relevant sanitizer/consumer checks.

The project owner implements substantive `.cpp` logic unless explicitly delegating it. Coding agents should primarily prepare headers, tests, CMake, small boilerplate, diagnostics, and docs.

### L0 success evidence

At minimum:

- valid/invalid schemas for every exact V1 kind;
- Integer/Number non-coercion;
- numeric ranges/finite rules;
- required/optional fields and unknown-field rejection;
- string/array/depth/node/field/enum/description bounds;
- invalid schema definitions rejected before use;
- a symmetric `Light{brightness: Integer 0..100}` fixture;
- an intentionally asymmetric codec proving read/write schemas stay distinct;
- every readable snapshot in a manifest validates against `readSchema`;
- representative write-schema candidates exercise the actual Lua codec decoder;
- exact Read/Write/ReadWrite projection from current `World` permission;
- access revocation/removal reflected by rebuild;
- unusual type/component names generate safe deterministic Lua expressions that resolve in the real sandbox;
- manifest data is copied and exposes no raw slot/pointer/registry;
- schema-less `expose_component(...)` remains source-compatible/executable but absent from model discovery;
- model metadata cannot enlarge actual Lua authority;
- current empty-array marker/literal limitation remains accurately represented;
- full existing tests remain green under strict warnings-as-errors.

### L0 out of scope

- any LLM/provider client;
- OpenAI-compatible API abstraction;
- Hermes dependency;
- MCP server/client;
- prompt orchestration/repair loop;
- behavior proposal persistence;
- automatic activation/approval;
- generic runtime inspection;
- cross-behavior cancellation;
- new DSL/IR;
- `SemanticTrigger`/adaptive state machine;
- real hardware/MQTT/voice/biosignals;
- Liquid Layer application policy;
- Solid Event Format v1 changes.

---

## L1 — Read-Only Liquid Runtime View

**Status:** Provisional — re-evaluate after L0.

**Question:** Can an external caller reconstruct relevant current Solid truth without registry access, shadow state, or conversational memory?

Likely scope:

- immutable owner-thread-built snapshots;
- API/model-safe behavior references;
- owned live intents with stable name, encoded value, target, priority, lifetime;
- selected/not-selected distinction;
- selected competitor/owner where applicable;
- authoritative observed external state;
- bounded command/report/evidence context;
- explicit frame/snapshot consistency semantics.

Known design questions:

- generic stable type/component names for `ComponentTarget` without registry exposure;
- mapping external component targets to observed state without exposing private Runtime bindings;
- ensuring live-intent facts and selection facts refer to one defined capture point;
- avoiding misuse of world-local generational handles as historical/global IDs.

Required acceptance: FocusSupport persistent `ON` loses to FollowUser `OFF`, remains live, and can win again after the competitor disappears. The view represents all of those facts accurately without model involvement.

---

## L2 — Behavior Proposal and Prospective Authoring Scope

**Status:** Provisional — re-evaluate after L1.

**Question:** Can an external model propose a new behavior without receiving a broadly privileged live behavior merely to discover what it could do?

Likely scope:

- immutable behavior proposal with exact Lua source and bounded review metadata;
- trusted prospective authoring scope chosen by host/application;
- capability manifest for that scope;
- source/capability/contract-version validation;
- stale-scope detection;
- explicit new-behavior versus approved-behavior-revision workflow;
- no model-selected arbitrary permissions.

Do not silently mutate an approved `LuaBehaviorScript` because context changed.

---

## L3 — Deterministic Proposal Evaluation

**Status:** Provisional — re-evaluate after L2.

**Question:** Can a candidate behavior be exercised through the real Solid execution path before activation?

Likely scope:

- isolated prepared `World`/`Runtime` fixture;
- real `LuaLifecycleSystem`/runner;
- real intent lifetime/resolution;
- real `InMemoryAdapter` for effects;
- deterministic evaluation scenarios;
- report separating intents, selections, commands, reports, observations, and final authoritative state;
- bounded failures suitable for an external repair loop.

The current light-specific `SimulationScenario` is not automatically a generic proposal evaluator; generalize only what L3 tests require.

---

## L4 — MCP Adapter

**Status:** Provisional — re-evaluate after L3.

**Question:** Can an agent runtime consume the same Liquid semantic API without gaining extra authority?

Likely first surface:

- focused discovery/capability tools;
- read-only runtime inspection;
- behavior validation/evaluation;
- no unrestricted mutators.

Protocol direction based on research current on 28 August 2026:

- implement against the current stable MCP revision at milestone start; current stable is `2026-07-28`;
- do not build new functionality around deprecated MCP Sampling/Roots/Logging;
- model invocation remains client/application-owned;
- MCP is a renderer/transport over Liquid's contracts;
- define structured input/output schemas where useful;
- keep tools few, focused, and context-scoped;
- tool descriptions/annotations never become authorization;
- validate every request/result server-side;
- preserve owner-thread confinement with immutable snapshots and marshalled mutation requests;
- prefer local/owner-controlled first deployment unless a real remote requirement justifies OAuth/network exposure.

SDK/language choice is deferred. As of 28 August 2026 official Tier 1 MCP SDKs are TypeScript, Python, Go, and C#; no official C++ SDK is listed.

Hermes is an integration proof, not contract owner. Current Hermes docs support protocol-era negotiation including the 2026 stateless probe. Test at least one non-Hermes client/conformance path.

---

## L5 — Approval, Activation, Revision, and Bounded Operations

**Status:** Provisional — re-evaluate after L4.

**Question:** Can reviewed model-produced behavior become live, be revised, or be stopped without source substitution, stale approval, or authority expansion?

Likely scope:

- approval bound to exact source/proposal hash;
- binding to authoring scope/access revision and evaluation evidence;
- installation of exact approved lifecycle revision;
- explicit revision workflow;
- explicit semantics for persistent intents owned by the prior revision;
- ownership-aware cancellation/removal justified by real scenarios;
- no generic `destroy_any_intent`/world mutation.

Changing `LuaBehaviorScript.source/revision` alone does not prove what happens to persistent intents already owned by that behavior. L5 must define this deliberately.

---

## L6 — Liquid Proposal Evidence and Change Surface

**Status:** Provisional — re-evaluate after L5.

**Question:** Can adaptive decisions be audited and can applications learn bounded changes without contaminating Solid's deterministic evidence contract?

Likely scope:

- separate Liquid proposal/evaluation/approval/revision records;
- correlation to Solid session/behavior/runtime evidence;
- optional provider/model metadata supplied by the application that actually performs inference;
- privacy-aware record of context/capabilities shared;
- bounded change cursor/feed only if Liquid Layer scenarios prove it necessary.

Liquid evidence answers why a behavior was proposed/revised/evaluated/approved/rejected. Solid evidence continues to answer what the deterministic Runtime actually executed and observed.

---

# Stage 3 — Liquid Layer

**Status:** Future.

Expected application work includes:

- neurodivergent-support scenarios;
- focus/task-initiation/transition/sensory/sleep support;
- application-specific inference triggers/revalidation policy;
- local/hosted model routing;
- Hermes or another agent runtime when useful;
- consent/privacy/data-export design;
- real device/smart-home integrations;
- simulation and user-study tooling.

Liquid Layer may teach an agent policies such as grace periods or when context changes deserve re-evaluation. Whenever possible, semantic reasoning should become deterministic Lua/intent policy so Solid continues correctly while models/agents are offline.

---

## Stage 2 Advancement Rule

For every Liquid milestone:

1. Start a short-lived branch from current `main`.
2. State the one missing capability being closed.
3. List existing Solid facilities reused before adding abstractions.
4. Define header/data/failure contracts before substantive implementation.
5. Write success, stale-state, permission, bound, and adversarial tests.
6. Add no provider/network dependency unless the milestone explicitly integrates it.
7. Preserve owner-thread and deterministic authority contracts.
8. Run strict full tests and relevant sanitizer/consumer checks.
9. Record completion evidence.
10. Re-evaluate the next provisional milestone instead of expanding automatically.
11. Update `AGENTS.md`, this file, and Stage 2 docs before widening scope.

A new abstraction must answer a demonstrated requirement current Solid/Liquid primitives cannot cleanly solve. Folder creation follows an accepted milestone, not speculative architecture.

---

## Current Notes

- Solid v0.1.0 is complete and frozen as the Stage 2 foundation.
- L0 is the only currently implementation-authorized Liquid milestone.
- Lua remains the generated executable behavior boundary.
- Liquid does not own model/provider choice.
- MCP is deferred until the semantic API exists and is independently testable.
- Hermes remains optional and replaceable.
- A model may be stateless between calls; current runtime truth is reconstructed from Liquid/Solid.
- Losing intent resolution does not kill a live intent.
- Application semantic triggers/revalidation remain Liquid Layer policy unless repeated scenarios prove a generic engine primitive is missing.
- The project owner implements substantive core `.cpp` logic unless explicitly delegating it.
