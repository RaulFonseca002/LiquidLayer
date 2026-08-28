# AGENTS.md — Liquid Development Context

This file is the working context for Codex or other coding agents inside the `liquid` repository.

Keep this file short and operational. Do not turn it into a full design document.

Read these Stage 2 references when working on L0:

- `docs/LIQUID_STAGE2_PLAN.md` — architecture, roadmap, research, deferred decisions;
- `docs/LIQUID_L0_IMPLEMENTATION_SPEC.md` — implementation-level L0 semantics and test expectations.

---

## Project Snapshot

**Liquid Layer** is the final application/research project: an adaptive smart-environment system focused on reducing cognitive friction for neurodivergent people.

**Liquid** is the standalone engine/framework that Liquid Layer will use.

Liquid is developed in stages:

1. **Solid** — deterministic ECS-inspired runtime, complete at v0.1.0 through M1-M6 and S0-S7.
2. **Liquid** — model-facing control, authoring, inspection, validation, and later bounded operation over Solid.
3. **Liquid Layer** — application policy: user/environment context, sensors, AI/model/agent selection, invocation policy, memory/conversation, and neurodivergent-support scenarios.

Stage 2 is active. The first approved implementation milestone is **L0 — Model-Facing Lua Capability Contract**.

Superposition ECS reference lives at `/home/raul/Desktop/superposition`. Use it only as a local design reference for ECS mechanics where relevant.

---

## Current Coding Milestone

### L0 — Model-Facing Lua Capability Contract

Goal:

> Make an existing prepared Solid behavior's Lua capabilities machine-readable, bounded, and host-verifiable without calling a model, adding MCP, changing runtime semantics, or activating generated behavior.

L0 extends the existing `Liquid::Lua` authoring boundary. It does not introduce a new adaptive runtime, provider abstraction, MCP server, or `Liquid::Adaptive` target.

Required concepts:

- a bounded `LuaValueSchema` matching the current `LuaValue` vocabulary;
- distinct trusted `readSchema` and `writeSchema` metadata for model-visible `LuaComponentCodec<T>` bindings because `encode` and `decode` are independent executable directions;
- a convenience symmetric metadata path for ordinary codecs whose read/write shapes are the same;
- optional short bounded trusted descriptions for binding/field meaning or units, without creating a domain ontology;
- an immutable `LuaCapabilityManifest` built through the existing `LuaBehaviorRunner` binding boundary from trusted metadata, the target behavior's current permissions, exact host-generated Lua access paths, copied readable values, current monotonic `now_ms`, and a stable Lua authoring-contract/version marker;
- deterministic schema/manifest validation and bounded diagnostics;
- current `World` permission, host-bound Lua closures, and the real codec decode remain final authority.

V1 schema vocabulary stays intentionally small and maps to exact `LuaValue` storage kinds:

- Boolean -> `bool`;
- Integer -> `std::int64_t`;
- Number -> finite `double` (no implicit Integer/Number coercion in L0);
- String -> `std::string` with explicit bounds and optional finite enum;
- Array -> `LuaValue::Array` with one item schema and count bounds;
- Object -> `LuaValue::Table` with named required/optional fields and unknown fields rejected by default.

Do not add null, bytes, numeric coercion, unions, `$ref`, regex constraints, arbitrary JSON Schema, or provider-specific schema keywords unless a real current Lua codec proves they are required.

Permission projection for a fully model-described binding must be exact:

| `World` permission | Manifest read side | Manifest write side |
|---|---|---|
| Read | `readSchema` + copied value | absent |
| Write | absent | `writeSchema` |
| ReadWrite | `readSchema` + copied value | `writeSchema` |

A snapshot is data, never authority. The host generates the exact Lua path expression; models must not reconstruct/escape component paths.

The current empty-array rule is part of the authoring contract: host-provided empty arrays keep their private Array marker, while literal `{}` is an empty Object. Do not invent an empty-array helper in L0 without a real codec requirement.

Trusted registration descriptions are bounded metadata. Dynamic runtime/user/device strings remain data and must not be promoted into trusted instructions by future renderers.

Model-facing views are immutable copies built on the owner thread. Do not introduce an async transport that calls `World`/`Runtime` from another thread.

### L0 allowed files

Expected implementation area:

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
docs/LIFECYCLE_SCRIPTING.md   # only if the public Lua contract changes
```

Small filename/API adjustments inside this existing `scripting/` boundary are allowed when tests make a cleaner shape obvious. Do not create speculative Stage 2 folders.

### L0 success evidence

At minimum test:

- valid and invalid schemas for every exact V1 kind;
- Integer/Number non-coercion;
- range, required-field, unknown-field, nesting, collection, string, enum, node, and description bounds;
- a symmetric `Light{brightness}` Lua codec with declared `0..100` read/write shape;
- an intentionally asymmetric test codec proving `encode`/read and `decode`/write schemas are not conflated;
- every emitted readable snapshot validates against its `readSchema`;
- representative write-schema values are exercised against the real codec decoder;
- Read/Write/ReadWrite manifest projection matches current `World` permission exactly;
- access revocation/removal is reflected by a rebuilt manifest;
- unusual names receive exact safe host-generated Lua path expressions and resolve in the real sandbox;
- manifest captures `now_ms` and the authoring-contract/version marker deterministically;
- current empty-array semantics remain accurately documented/tested;
- manifest data contains copies, never raw slots/pointers/registries;
- the existing schema-less `expose_component(...)` path remains source-compatible and executable but absent from model discovery;
- model schema/description metadata cannot enlarge actual Lua/World authority;
- strict full suite remains green.

Do not claim formal equivalence between arbitrary executable C++ codecs and schema metadata. The real codec remains final validation.

---

## Stage 2 Boundary

The accepted architecture is:

```text
Liquid Layer
    chooses local/hosted model or agent runtime
    decides when to invoke it and what application context means
        │
        ▼
Liquid
    exposes focused model-facing semantic capabilities
    discover / author / observe / validate / evaluate / bounded operation
        │
        ▼
Solid
    deterministic runtime authority and evidence
```

Important consequences:

- Liquid does not choose providers/models or decide local versus cloud.
- Direct inference and agent execution are separate application choices over the same Liquid surface.
- Hermes is a future external consumer/integration target, not a Liquid dependency.
- MCP is a future transport adapter over Liquid's semantic API, not Liquid's internal domain model.
- Do not add a `ModelPort` unless a future engine-level requirement proves Liquid itself must invoke a model.
- Do not introduce `SemanticTrigger`, `BehaviorCondition`, or equivalent adaptive abstractions until multiple real scenarios prove existing Lua/components/intents/application orchestration are insufficient.
- Changing context does not silently mutate an approved `LuaBehaviorScript`; changing source is an explicit behavior revision.
- An external model may be stateless between calls, but current runtime truth must be reconstructed from Liquid/Solid rather than trusted from agent memory.
- Provider-side structured/constrained output is useful generation assistance, never an authority boundary; Liquid/Solid validate locally.

See `docs/LIQUID_STAGE2_PLAN.md` for the rationale and provisional roadmap. For L0 implementation details, `docs/LIQUID_L0_IMPLEMENTATION_SPEC.md` wins over older brainstorm wording.

---

## Branch Workflow

This project uses one GitHub repository, `RaulFonseca002/tcc`, with one long-lived branch.

- `main` is the single long-lived development branch.
- `experiment/stage2` is retired; do not develop on it.
- Start each implementation milestone/change on a short-lived branch from current `origin/main`.
- Never force-push or rebase published `main`.
- Run the strict full suite before merge: `-DLIQUID_ENABLE_STRICT_WARNINGS=ON -DLIQUID_WARNINGS_AS_ERRORS=ON`, then `ctest --output-on-failure`.
- Standing CI is Linux strict GCC/Clang, ASan/UBSan, TSan, Core coverage, and Core-only consumers. AppleClang/MSVC portability is manually dispatched when appropriate.
- Engine/tool separation is enforced at the CMake packaging boundary. Solid Scope remains an owner-operated development instrument and never becomes a second Runtime.

---

## Current Repository Shape

Do not create folders before an approved milestone needs them. `CMakeLists.txt` is the authoritative source-file inventory.

```text
liquid/
  CMakeLists.txt
  AGENTS.md
  DEVELOPMENT_TRACKING.md
  Liquid_Concepts_and_Architecture.md
  COMPLETE_SOLID.md

  docs/
  apps/
  include/liquid/
  src/
    world/
    runtime/
    events/
    effects/
    scripting/
  tests/
  fuzz/
  examples/
  third_party/
```

Src-private headers are never installed or treated as consumer API.

---

## Coding Role

Codex should primarily generate:

- headers;
- tests;
- CMake files;
- small boilerplate;
- compile fixes;
- documentation updates.

The project owner implements substantive core `.cpp` logic unless explicitly asking an agent to do so. Do not silently implement large runtime or behavior logic.

---

## Frozen Solid Rules Relevant to Liquid

### Authority and runtime

- `Runtime` owns the deterministic frame loop and is its sole phase driver.
- `World` is the public state/behavior/component lifecycle boundary.
- `Coordinator`, registries, `WorldState`, storage internals, raw slots, and mutable pointers are not model-facing API.
- `World` and `Runtime` are single-thread-confined. Hosts provide broader synchronization.
- External state changes only after authoritative reports or revisioned observations; selected desire is not physical truth.

### Behaviors and agents

- A behavior is the main Solid domain identity and owns its intents.
- Behavior logic belongs in systems/components such as the Lua lifecycle system, not virtual behavior objects.
- Do **not** assume an external adaptive agent must itself be represented by an `Agent` component or Solid behavior. Stage 2 agents may live outside Solid and consume Liquid's model-facing API.

### Handles

- Public `BehaviorId`, `IntentId`, and component-slot handles are world-bound generational handles carrying world identity, slot, and 32-bit generation.
- Stale and cross-world handles are rejected; exhausted generations retire slots.
- `ComponentType<T>` is also world-bound/versioned as implemented by v0.1.
- `SessionId`, `CommandId`, and `RecordId` are monotonic/non-recyclable in their documented scopes.
- Runtime handles are not permanent historical identities and should not be exposed to models when a stable semantic/opaque view can be used instead.

### Intents

- Intents are immutable after creation.
- Current lifetime policies are `Persistent` and `UntilTime`; explicit cancellation destroys an intent.
- A live intent that loses resolution remains alive. Losing selection is not cancellation.
- Behaviors own intents; owner/target indexes and cleanup remain registry-owned.
- World-created component intents require current write permission and are cleaned if the target disappears or permission is lost.
- Do not add LLM arbitration to deterministic conflict resolution by default.

### Lua boundary

- Generated executable behavior uses the existing lifecycle Lua path unless a later milestone explicitly proves another representation is necessary.
- Lua gets a fresh VM per behavior/frame. Persistent state belongs in normal serializable components.
- Lua gets only typed, named, allowlisted capabilities, copied snapshots, a host-fixed owner, and host-fixed monotonic time.
- Lua never receives `World`, registries, storage, raw slots/pointers, or arbitrary owner selection.
- Named proposals, cancellations, and watches validate/commit transactionally; failed bundles leave no partial mutation.
- `solid.owned_intents` exposes only opaque snapshots of the executing behavior's own named live intents.
- `LuaComponentCodec<T>::encode` and `decode` are separate executable directions; do not assume symmetric model-facing shapes.
- The executable authoring contract is in `docs/LIFECYCLE_SCRIPTING.md` and current scripting headers. Stage 2 model-facing metadata/roadmap is in the two Liquid Stage 2 docs above.

### Evidence

- Solid evidence answers what the deterministic runtime executed and observed.
- Do not put model prompts, agent memory, or proposal rationale into frozen Solid Event Format v1.
- A future Liquid proposal journal may reference Solid evidence without replacing it.

---

## What Not to Build in L0

Do not add:

- model-provider clients or API keys;
- OpenAI/Anthropic/llama.cpp/vLLM/Hermes integration;
- MCP server/client dependencies;
- prompt builders or autonomous repair loops;
- behavior activation/approval flow;
- generic runtime-inspection or mutating remote tools;
- a new behavior DSL/IR;
- adaptive trigger abstractions;
- MQTT or real hardware adapters;
- voice or biosignal pipelines;
- Liquid Layer application policy;
- shared-library ABI promises;
- changes to Solid Event Format v1;
- multi-writer/network-filesystem event stores;
- encryption/tamper-evidence claims.

---

## Milestone Advancement

When L0 is complete:

1. record its regression/verification evidence in `DEVELOPMENT_TRACKING.md`;
2. re-evaluate the provisional next milestone from `docs/LIQUID_STAGE2_PLAN.md` using what L0 actually taught us;
3. update this file to the newly approved scope;
4. expand repository structure only when that milestone requires it.

Do not automatically implement the entire provisional L1-L6 ladder.

---

## Development Style

- Baby steps.
- Small tests first.
- Minimal headers.
- No speculative folders.
- No large rewrites without explicit request.
- Prefer clear ownership over clever abstractions.
- Prefer existing Solid primitives until a missing capability is demonstrated.

### C++ Formatting Preferences

- Use 4 spaces for indentation.
- Put opening braces on the same line for functions, classes, and control blocks.
- Keep simple one-line guard clauses readable; braces are not required for a single obvious statement.
- Prefer the current project style over adding qualifiers/boilerplate without a concrete benefit.
- For new classes, keep access sections visually simple.
- Do not reformat unrelated existing code just to normalize style.
