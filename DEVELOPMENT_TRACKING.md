# DEVELOPMENT_TRACKING.md — Liquid Project Tracking

This file tracks the practical development path for the `liquid` engine.

The goal is to keep each active milestone small, testable, and explicit. Detailed Solid v0.1 completion evidence lives in `COMPLETE_SOLID.md` and `docs/TRACEABILITY.md`; Stage 2 architecture and rationale live in `docs/LIQUID_STAGE2_PLAN.md`.

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

Stage 1 is frozen at Solid v0.1.0. Stage 2 may extend the framework only through owner-approved Liquid milestones and must not weaken the Solid contracts to make model integration easier.

Current milestone: **L0 — Model-Facing Lua Capability Contract**.

The accepted Stage 2 boundary is:

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

The detailed implementation and release audit is intentionally not duplicated here. See:

- `COMPLETE_SOLID.md` — accepted completion gates;
- `docs/TRACEABILITY.md` — milestone/test traceability;
- `docs/PUBLIC_API.md` — public framework contract;
- `docs/LIFECYCLE_SCRIPTING.md` — current Lua lifecycle contract;
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
- **S0-S7** — Solid v0.1 framework finalization, hardening, Scope, packaging, and release audit.

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
- Existing Lua lifecycle proposal/cancellation bundles commit transactionally.

---

# Stage 2 — Liquid

## L0 — Model-Facing Lua Capability Contract

**Status:** Current — approved for implementation.

### Goal

Make the existing Lua behavior boundary machine-readable enough for an external model/agent to author against it without adding a model provider, MCP transport, new runtime authority, or a second behavior language.

The smallest complete L0 circuit is:

```text
trusted Lua component binding
    + trusted model-facing schema metadata
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

- bounded `LuaValueSchema` matching the values actually transported by `LuaBehaviorRunner`;
- optional bounded trusted descriptions sufficient to explain component/field semantics without creating an ontology;
- model-visible schema metadata registered beside the existing `LuaComponentCodec<T>` binding;
- immutable `LuaCapabilityManifest` for one prepared behavior and current access state;
- exact host-generated Lua access-path expressions;
- explicit read/write/read-write projection from current `World` permissions;
- copied readable snapshots validated against declared schema;
- current monotonic `now_ms` and a stable authoring-contract/version marker when exposed to a model-facing renderer;
- explicit limits on schema depth/size and manifest size;
- deterministic bounded diagnostics.

### V1 schema vocabulary

Keep the vocabulary intentionally smaller than JSON Schema and aligned with current `LuaValue`:

- Boolean;
- signed Integer with optional bounds;
- finite Number with optional bounds;
- String with explicit size bounds and optional finite enum;
- Array with one item schema and item-count bounds;
- Object with named required/optional fields; reject unknown fields by default.

Do not add until a real current codec requires them:

- null;
- bytes;
- unions/composition;
- `$ref` or recursive schema graphs;
- regular-expression constraints;
- arbitrary user-provided JSON Schema;
- provider-specific schema keywords.

The canonical schema remains a Liquid type. Later adapters may render it to MCP JSON Schema or provider-specific structured-output schemas.

### Authority rules

Schema metadata is descriptive validation, not authority.

- The real `World` permission remains final.
- The host-bound Lua closure remains final.
- `LuaComponentCodec<T>::decode` remains final value validation.
- Transactional commit remains final mutation validation.
- Missing or incorrect model metadata must fail closed for discovery; it must never widen executable authority.
- Model-facing descriptions are trusted bounded registration metadata. Runtime/user strings and values are data and must not become executable instructions through naive prompt concatenation.

Permission projection:

| Current permission | readable snapshot | writable schema |
| --- | ---: | ---: |
| Read | yes | no |
| Write | no | yes |
| ReadWrite | yes | yes |

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
docs/LIFECYCLE_SCRIPTING.md      # only when the public Lua contract changes
```

Small filename/API-shape adjustments inside the existing `scripting/` boundary are allowed when tests make a cleaner shape obvious. Do not create speculative `adaptive/`, provider, agent, or MCP directories in L0.

### L0 implementation order

1. Write `LuaValueSchema` value/limit tests first.
2. Draft the minimal public schema header.
3. Implement schema construction and validation.
4. Add real codec fixtures, including `Light{brightness: 0..100}`.
5. Write capability-manifest permission/copy/path tests.
6. Draft the minimal manifest API and additive schema-bearing Lua binding registration.
7. Implement manifest construction on the owner thread using the same current permissions/snapshots as Lua execution.
8. Add stale/revoked-access and hostile/unusual-name cases.
9. Update CMake and the scripting contract documentation.
10. Run strict full regression and sanitizer-relevant checks before merge.

The project owner implements substantive `.cpp` logic unless explicitly delegating it. Coding agents should primarily prepare headers, tests, CMake, small boilerplate, diagnostics tables, and docs.

### L0 success evidence

At minimum:

- valid and invalid schemas for every V1 kind;
- integer/number range enforcement;
- required/optional field behavior;
- unknown-field rejection;
- string/array/depth/node/description bounds;
- invalid schema definitions rejected at construction/configuration time;
- a real Lua codec whose declared schema accepts valid snapshots and rejects invalid ones;
- every readable snapshot emitted into a manifest validates against its declared schema;
- Read/Write/ReadWrite manifest projection exactly matches current `World` permission;
- access revocation/removal is reflected in a newly built manifest;
- unusual type/component names receive exact safe host-generated Lua expressions;
- manifest values are immutable copies, never retained component pointers or registry references;
- the existing schema-less `expose_component(...)` path remains source-compatible;
- schema metadata cannot enlarge actual Lua/World authority;
- diagnostics and manifests remain within explicit limits;
- full existing tests remain green under strict warnings-as-errors.

### L0 out of scope

- any LLM/provider client;
- OpenAI-compatible API abstraction;
- Hermes dependency;
- MCP server/client;
- prompt-orchestration/repair loop;
- behavior proposal persistence;
- automatic activation/approval;
- generic runtime inspection;
- cross-behavior cancellation;
- a new DSL/IR;
- `SemanticTrigger`/adaptive state-machine abstraction;
- real hardware/MQTT/voice/biosignals;
- Liquid Layer application policy;
- changes to Solid event format v1.

---

## L1 — Read-Only Liquid Runtime View

**Status:** Provisional — re-evaluate after L0.

**Question:** Can an external caller reconstruct relevant current Solid truth without registry access, shadow state, or conversational memory?

Likely scope:

- immutable owner-thread-built snapshots;
- behavior references suitable for model/API use;
- owned live intents with stable name, encoded value, target, priority, lifetime;
- selected/not-selected distinction;
- selected competitor and owner when applicable;
- authoritative observed external state where available;
- bounded command/report/evidence context;
- explicit snapshot/frame consistency semantics.

Known design questions to resolve in L1:

- how to identify component type/name generically without exposing `ComponentRegistry`;
- how to map a selected external component to observed state without exposing Runtime effect bindings;
- whether the view represents the last completed frame or an explicitly captured host snapshot;
- how opaque behavior references survive only within their documented world/session scope.

Required acceptance scenario: a persistent FocusSupport `ON` intent loses to a FollowUser `OFF` intent, remains live, and can become selected again after the competitor disappears. The view must represent those facts without calling a model.

---

## L2 — Behavior Proposal and Prospective Authoring Scope

**Status:** Provisional — re-evaluate after L1.

**Question:** Can an external model propose a new behavior without receiving a broadly privileged live behavior merely to discover what it could do?

Likely scope:

- immutable behavior proposal containing exact Lua source and bounded review metadata;
- host/application-selected prospective authoring scope;
- capability manifest for that scope;
- stale-scope detection;
- separation between a new behavior proposal and a revision of an approved behavior;
- no model-selected arbitrary permissions.

Do not silently mutate an approved `LuaBehaviorScript` because context changed. A source change is an explicit revision operation.

---

## L3 — Deterministic Proposal Evaluation

**Status:** Provisional — re-evaluate after L2.

**Question:** Can a candidate behavior be exercised through the real Solid execution path before activation?

Likely scope:

- isolated prepared `World`/`Runtime` fixture;
- real `LuaLifecycleSystem` and `LuaBehaviorRunner`;
- real intent resolution;
- real `InMemoryAdapter` for external effects;
- deterministic evaluation scenarios;
- report that keeps proposed intents, selected desires, commands, reports, and observed state separate;
- bounded failures suitable for a caller/model repair loop.

Do not treat the current light-specific `SimulationScenario` as a generic proposal evaluator until this milestone earns that abstraction.

---

## L4 — MCP Adapter

**Status:** Provisional — re-evaluate after L3.

**Question:** Can an agent runtime consume the same Liquid semantic API without gaining extra authority?

Likely first surface:

- discovery/capability tools;
- read-only runtime inspection tools;
- behavior proposal validation/evaluation tools;
- no unrestricted mutators.

Protocol direction based on research current on 28 August 2026:

- implement against the current stable MCP revision at milestone start; current stable is `2026-07-28`;
- do not build new functionality around deprecated MCP Sampling/Roots/Logging;
- MCP remains a renderer/transport over Liquid's own contracts;
- define both input and structured output schemas for tools;
- keep tools few, focused, and context-scoped;
- treat MCP annotations/descriptions as hints, never authorization;
- validate every request server-side;
- preserve Solid owner-thread confinement through immutable snapshots and marshalled mutation requests;
- prefer local/owner-controlled transport first unless a real requirement justifies remote OAuth/network exposure.

SDK/language choice is deferred. As of 28 August 2026 the official Tier 1 SDKs are TypeScript, Python, Go, and C#; there is no official C++ SDK. Do not add a community C++ MCP dependency before L4 re-evaluates the ecosystem.

Hermes is an integration proof, not the contract owner. At least one non-Hermes client/conformance test should prove the surface is generic.

---

## L5 — Approval, Activation, Revision, and Bounded Operations

**Status:** Provisional — re-evaluate after L4.

**Question:** Can reviewed model-produced behavior become live, be revised, or be stopped without source substitution, stale approval, or authority expansion?

Likely scope:

- approval bound to exact proposal/source hash, authoring scope/revision, and evaluation evidence;
- installation of the exact approved lifecycle source;
- explicit revision workflow;
- explicit semantics for what happens to intents owned by the previous approved revision;
- ownership-aware cancellation/removal operations justified by real scenarios;
- no generic `destroy_any_intent` or arbitrary world mutation.

The revision/intents interaction must be designed deliberately: changing script revision must not accidentally leave old persistent intents with semantics the new source no longer owns or understands.

---

## L6 — Liquid Proposal Evidence and Change Surface

**Status:** Provisional — re-evaluate after L5.

**Question:** Can adaptive decisions be audited and can applications learn about bounded changes without contaminating Solid's deterministic evidence contract?

Likely scope:

- Liquid proposal/evaluation/approval records separate from Solid event format v1;
- correlation to Solid session/behavior/runtime evidence;
- optional provider/model metadata supplied by the application that actually performed inference;
- record of what context/capabilities were shared, subject to application privacy policy;
- bounded change cursor/feed only if Liquid Layer scenarios prove it necessary.

Liquid records answer why a behavior was proposed/revised/approved/rejected. Solid records continue to answer what the deterministic runtime actually executed and observed.

---

# Stage 3 — Liquid Layer

**Status:** Future.

Expected application work includes:

- neurodivergent-support scenarios;
- focus/task-initiation/transition/sensory/sleep support;
- application-specific inference triggers and revalidation policy;
- local/hosted model routing;
- Hermes or other agent-runtime orchestration when useful;
- consent/privacy/data-export design;
- real device integrations and smart-home adapters;
- simulation and user-study tooling.

Liquid Layer may teach an agent application policies such as grace periods or when context changes deserve re-evaluation. Whenever possible, semantic reasoning should be compiled into deterministic Lua/intent policy so Solid can continue correctly while all models/agents are offline.

---

## Stage 2 Advancement Rule

For every Liquid milestone:

1. Start a short-lived branch from current `main`.
2. State the one missing capability being closed.
3. List the existing Solid facilities reused before adding new abstractions.
4. Define header/data/failure contracts before substantive implementation.
5. Write success, stale-state, permission, bound, and adversarial tests.
6. Add no provider/network dependency unless that milestone exists specifically to integrate it.
7. Preserve owner-thread and deterministic authority contracts.
8. Run strict full tests and relevant sanitizers/consumer checks.
9. Record completion evidence.
10. Re-evaluate the next provisional milestone instead of expanding it automatically.
11. Update `AGENTS.md`, this file, and `docs/LIQUID_STAGE2_PLAN.md` before widening scope.

A new abstraction must answer a demonstrated requirement that current Solid/Liquid primitives cannot cleanly solve. Folder creation follows an accepted milestone, not speculative architecture.

---

## Current Notes

- Solid v0.1.0 is complete and frozen as the Stage 2 foundation.
- L0 is the only currently implementation-authorized Liquid milestone.
- Lua remains the generated executable behavior boundary.
- Liquid does not own model/provider choice.
- MCP is deferred until the semantic API exists and is independently testable.
- Hermes remains optional and replaceable.
- A model may be stateless between calls; current runtime truth must be reconstructed from Liquid/Solid.
- Losing intent resolution does not kill a live intent.
- Application-specific semantic triggers/revalidation remain Liquid Layer policy unless repeated scenarios prove a generic engine primitive is missing.
- The project owner implements substantive core `.cpp` logic unless explicitly delegating it.