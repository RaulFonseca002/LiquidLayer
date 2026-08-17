# AGENTS.md — Liquid Development Context

This file is the working context for Codex or other coding agents inside the `liquid` repository.

Keep this file short and operational. Do not turn it into a full design document.

---

## Project Snapshot

**Liquid Layer** is the final application/research project: an adaptive smart-environment system focused on reducing cognitive friction for neurodivergent people.

**Liquid** is the standalone engine/runtime that Liquid Layer will use.

Liquid is being developed in stages:

1. **Solid** — deterministic ECS-inspired runtime.
2. **Liquid** — adaptive/LLM layer that generates or modifies solid behavior blocks.
3. **Liquid Layer** — final neurodivergent-support application built on top of Liquid.

Stage 1, **Solid**, is complete through M6. The current work is limited to defining and approving the first Stage 2, **Liquid**, milestone.

Superposition ECS reference lives at `/home/raul/Desktop/superposition`. Use it as a local design reference for component managers, coordinator-owned signatures, and template-driven component type lookup.

---

## Current Coding Milestone

### Solid v0.1 Finalization

M1 through M6 complete the original deterministic decision core. The approved current work is the S0-S7 Solid finalization program in `COMPLETE_SOLID.md` and `DEVELOPMENT_TRACKING.md`, ending in a private reusable C++20 framework release at version 0.1.0.

Current milestone: **complete S6 with Solid Lifecycle Scripting and the Full-Loop Scope Simulation on `main`**. The lifecycle core and Scope now live on the one `main` branch; Scope must exercise that one Runtime through script proposal, resolution, command dispatch, simulated feedback, and authoritative component projection. The remote CI matrix runs green on `main` (first full pass 2026-08-17); keeping it green is a standing gate.

Codex may add only the files required by the approved ordered milestone and its regression evidence. All work belongs on a short-lived branch from `origin/main`. Solid Scope remains an optional Unix-only development application and must not introduce a second Runtime; its Python bridge regressions are Linux-verified in CI.

The approved finalization includes public API hardening, world-bound generational handles, bounded encoded values and codecs, transactional component replacement, durable records and replay, effects/commands/feedback, deterministic retry and reconciliation, simulation, packaging, and the accepted Solid Scope fixes. It does not include LLM integration, MQTT, voice, biosignals, real hardware, or final Liquid Layer behavior.

---

## Branch Workflow

This project uses one GitHub repository, `RaulFonseca002/tcc`, with one long-lived branch.

- `main` is the single development branch. It carries the Solid engine and the development instruments (Solid Scope, trace tooling, test applications) in one tree.
- Engine/tool separation is enforced at the CMake packaging boundary, not by branches: installed consumers receive only the `Liquid::Core`, `Liquid::Lua`, and `Liquid::Simulation` targets; `apps/`, the visualizer, and their tests are never installed or exported.
- `experiment/stage2` is retired. It was the former visualization track and was fast-forwarded into `main` at its final commit; do not develop on it.
- Never force-push or rebase `main` after it has been published.

### Making a Change

1. Start a short-lived branch from current `origin/main`.
2. Implement the smallest complete change with its regression evidence.
3. Run the strict full suite (`-DLIQUID_ENABLE_STRICT_WARNINGS=ON -DLIQUID_WARNINGS_AS_ERRORS=ON`, `ctest --output-on-failure`).
4. Merge into `main` through its normal review path and confirm the remote CI matrix stays green (strict Release on Linux GCC/Clang, macOS AppleClang, Windows MSVC, plus ASan/UBSan, TSan, the Core coverage gate, and the Core-only consumers).
5. Core changes must not depend on visualization code; Scope changes must not alter Solid core semantics. The Python bridge regressions run on Linux CI only — the bridge is an owner-operated Linux instrument — while macOS compiles all Scope targets and runs the C++ scenario, trace, and renderer self-test coverage.

### Future Separation

The intended "proper" separation is a later split of Solid Scope into its own repository that consumes the engine through `find_package(Liquid)` like any other installed consumer. The natural moment is Stage 2 kickoff or the first post-0.1 release. Until then, do not create new long-lived branches, and update this section before moving any code to a second repository.

---

## Current Repository Shape

Do not create folders before an approved milestone needs them. The map below is
directory-level; the CMake source list in `CMakeLists.txt` is the authoritative
file inventory.

```text
liquid/
  CMakeLists.txt
  AGENTS.md
  DEVELOPMENT_TRACKING.md
  Liquid_Concepts_and_Architecture.md
  ARTICLE_NOTES.md
  COMPLETE_SOLID.md

  docs/            # frozen v0.1 contracts and operational guides
  apps/            # simulation CLI, trace executable, Solid Scope visualizer
  include/liquid/  # public headers; detail/ is internal implementation
  src/
    world/         # World and Coordinator
    runtime/       # Runtime frame driver, effects state, evidence,
                   # feedback, restore; src-private RuntimeInternals.hpp
    events/        # event stores, on-disk format codec, value codec, replay
    effects/       # effect types, feedback channel, idempotent dispatcher
    scripting/     # sandboxed Lua boundary
  tests/           # Catch2 suites and golden fixtures
  fuzz/            # decoder fuzz targets
  examples/        # source-tree and installed-package consumers
  third_party/     # pinned Catch2 and Lua distributions
```

Allowed to add new folders only when a milestone explicitly requires them.
Src-private headers (for example `src/runtime/RuntimeInternals.hpp` and the
`src/events/FileEventStore*.hpp` pair) are included by quoted relative path,
are never installed, and never appear in `include/liquid`.

---

## Coding Role

Codex should primarily generate:

- headers;
- tests;
- CMake files;
- small boilerplate;
- compile fixes;
- documentation updates.

The project owner implements the core `.cpp` logic unless explicitly asked otherwise.

Do not silently implement large behavior or runtime logic.

---

## Solid Core Rules

### Behaviors

- A behavior is the main domain identity in the Solid runtime.
- Agents later will be behaviors with an `Agent` component.
- Behaviors own the intents created on their behalf.
- Behavior logic belongs in systems that process behavior components, not in virtual behavior objects.

### Intents

- Intents are immutable after creation.
- Behaviors, agents, systems, events, or schedules may create intents with a `BehaviorId` owner/source.
- Intents request component-state changes or external effects; they are not stored as components.
- `IntentRegistry` resolution may select or ignore an intent.
- Lifetime/cleanup logic may expire an intent.
- Cleanup may delete an intent, and explicit deletion is the current cancellation model.
- No system should mutate the semantic contents of an existing intent after creation.
- `IntentRegistry` owns immutable typed intent records as the source of truth and keeps secondary indexes for owner and target lookup.
- `IntentRegistry` owns intent data, indexes, expiration cleanup during resolution, and deterministic intent selection.
- Intent targets use `ComponentTypeId + ComponentSlotId` for component-state requests and are indexed as type -> slot -> intent IDs.
- `IntentRegistry` does not validate whether an owner `BehaviorId` exists.
- Valid behavior ownership should be enforced before calling intent APIs, normally through `World` or behavior creation flow.
- World-created component intents require current write access to their target slot.
- World cleanup destroys intents whose target slot is removed or whose owner loses write access to that target.
- `World::create_behavior()` must create the matching intent pool, and behavior destruction must remove that pool, so behavior state and intent-manager behavior pools stay aligned.
- Manager command functions such as `IntentRegistry::destroy(IntentId)` may assume valid live handles from the project flow; query functions such as `exists(...)` can still return false safely.
- Prefer making invalid states unreachable at the call boundary over repeating defensive checks in every lower-level manager.

### IDs

Current planned ID model:

- `BehaviorId`: 16-bit value, world-local, recyclable.
- `IntentId`: 32-bit value, world-local, recyclable.
- `ComponentTypeId`: 16-bit value used for registered component types and signatures.
- `Signature`: component-type bitset used by both behavior composition and system requirements.
- `ComponentType<T>`: typed runtime handle returned by component registration; outside code should keep this handle and pass it back to component APIs.
- `ComponentSlotId`: 16-bit value used as the slot handle inside one typed component storage.
- Packed composite keys may combine two 16-bit values into one 32-bit value when the relationship is structural and stable.
- `IntentId` is a global recyclable intent-record handle; owner and target are stored on the intent record and indexed separately.
- IDs are handles inside the current world state, not permanent historical IDs.
- Logs/replay later use frame/log/event identifiers for historical uniqueness.

### Components

- Components are plain data.
- Component types are the signatures systems query for.
- Component storage is only a typed slot pool for one component type.
- Component names are owned by `ComponentRegistry`, not by `ComponentStorage`.
- Component names are globally unique per component type, such as `Light` plus `"officeLight"`.
- Component types are registered with explicit stable names and return `ComponentType<T>` handles, as established in M1.
- `TypeName -> ComponentTypeId` happens at registration; after that, `ComponentTypeId` is the internal runtime key for storage, name maps, and behavior signatures.
- Behaviors do not own components exclusively. Multiple behaviors may receive read/write access to the same named component.
- `ComponentRegistry` owns component type IDs, the type-erased storage map, and the `ComponentName <-> ComponentSlotId` indexes for each component type.
- `ComponentStorage<T>` owns component slots, slot recycling, and access records for that one component type.
- Live component slots are represented explicitly; removal destroys the stored value immediately, and liveness checks are constant-time.
- References and pointers returned by component APIs are borrowed only until the next structural mutation of that typed storage. Never retain them across frames or expose them to Lua.
- Behavior access is tracked inside the typed storage as `BehaviorId -> vector<ComponentSlotAccess>`, where each access records a slot and read/write mode.
- In M4, `World` is the outside-facing state boundary and owns `WorldState`. `Runtime` is the sole frame-phase driver; `Coordinator` is internal consistency logic over `WorldState`.
- The settled M4 order is `begin -> expire -> systems -> resolve -> end`, so intents created by systems can participate in the same frame.
- World and system topology cannot change during system dispatch. Systems may use component data through existing APIs and create or cancel intents.
- Systems request `name -> ComponentSlotId` maps for a behavior and component type. Behavior permission checks happen before component slots are handed out or resolved.
- System registration receives its initial `Signature` atomically. Only `Coordinator` derives membership from behavior signatures; `World` has no public manual membership override.
- Membership callbacks are observational and should not throw or mutate topology. The registry commits all transitions and then rethrows the first callback error; creation/registration may roll back the newly created object, while already-started structural changes remain committed and consistent.
- Physical addresses or adapter references may belong inside component data when needed; permissions belong in the behavior/type/slot access table.
- Components should not contain virtual behavior.
- Do not use C++ inheritance between components.
- Use composition: common components plus specific components.
- Script behavior should be represented as component data processed by a system later; the completed M1 core only provides the storage/query foundation for that path.

### Lifetime

- Behavior lifetime may be modeled with behavior components when needed.
- Intent lifetime is intent metadata, not component storage.
- M2 introduced the minimal lifetime model needed to evaluate and clean expired immutable intents.
- Current lifetime policies are persistent and until-time. Explicit cancellation is represented by destroying the intent. Until-frame, cancellation events, or script-based lifetime can remain future work unless explicitly required.
- `IntentTime` is monotonic session-relative time in milliseconds, never wall-clock or epoch time. Explicit frame times must be nondecreasing.
- Factories/bundles should be used later so required intent fields and behavior component sets are not forgotten.

### Runtime and Lua Boundary

- `World` and `Runtime` are single-thread-confined; callers provide any external synchronization.
- Lua receives only typed, named, allowlisted capabilities for creating new intents. It never receives `World`, `Coordinator`, registries, storage, raw component slots, component pointers, or a caller-selected owner.
- Cached capability layouts contain only immutable host descriptions keyed by lifecycle-unique world and behavior access revisions. Lua gets a fresh state, table, and copied snapshot on every execution.
- Access-table contents are data, not authority. Writable entries expose only `propose(request)`, and one execution may buffer multiple proposals for the same target before any intent is committed.
- The host fixes the executing `BehaviorId` and current time. Lua may request only persistent lifetime or a checked duration in milliseconds.
- Each execution has bounded instructions, Lua memory, buffered host values, strings, tables, source size, diagnostic size, and created-intent count.
- A failed script returns an execution error and destroys only the intents created by that execution. Script errors must not escape `System::run` and fault the whole runtime.
- Lua C closures catch C++ exceptions before returning to Lua, and Lua error jumps must not cross live C++ RAII objects.
- `Liquid_Concepts_and_Architecture.md`, section 13, is the canonical Lua script-authoring and future model-prompt contract. Generated scripts also require a trusted dynamic capability manifest; never infer codec schemas or permissions from snapshots alone.

### Serialization/Reproducibility

Design headers so the data can later be serialized and replayed.

Avoid:

- raw function pointers in components;
- hidden mutation paths;
- polymorphic component objects;
- runtime-only state as the only source of truth.

---

## Current Success Criteria

S1-S5 are complete. S6 is complete when:

1. The reviewed lifecycle and Scope work lands on `main` with the complete suite and remote CI matrix green.
2. Trace schema v2 separates desire, command, attempt, result, retry, timeout, and observed-state evidence.
3. The bridge enforces bounded parsing/validation and supervises the complete trace process group on Unix.
4. Incremental rendering and batched catch-up remain responsive for 1,000-frame traces.
5. The dependency-free browser self-test covers presentation paths, and the
   owner manually verifies streaming, controls, malformed data,
   reconnect/error states, and responsiveness. No Playwright or Node package
   dependency is required for this internal development instrument.
6. The full suite passes with one Runtime and no semantic divergence between Scope and the Solid core.

---

## What Not to Build Yet

Do not add these during Solid v0.1 finalization:

- LLM integration;
- MQTT or real hardware adapters;
- voice or biosignal pipelines;
- final Liquid Layer application concepts;
- shared-library ABI promises;
- multi-writer or network-filesystem event stores;
- encryption or tamper-evidence claims.

## Future Notes

- M1 is complete: ECS-style behavior identity, typed component storage/registry, coordinator-owned signatures, intent owner pools, system membership, expanded assert tests, stress tests, and opt-in sanitizer builds are in place.
- M2 intent lifetime and expiration is complete and now flows through `World` at the public boundary.
- M3 intent resolution is complete: behavior/type access produces `name -> ComponentSlotId`, `IntentRegistry` returns `name -> selected IntentId`, and later application resolves component data through coordinator/registry APIs.
- System coordination is implemented as template-addressed system registration, signatures, behavior membership, and membership callbacks.
- M4 minimal frame loop is complete: `Runtime` alone drives `begin -> expire -> systems -> resolve -> end`, records completed or failed frames, accepts only nondecreasing explicit time, and runs systems in deterministic registration order.
- M5 Lua behavior scripting is complete: Lua may create new intents only through controlled APIs and cannot mutate existing intent records or bypass `World`.
- M6 Simulation CLI is complete: it provides a deterministic, inspectable, hardware-free way to exercise the completed Solid core.
- `Coordinator` remains responsible internally for registering systems, storing or forwarding system `Signature`s, matching behavior signatures to systems, and updating system membership whenever component access changes or a behavior/component is destroyed.
- Future systems, runtime loops, and adapters should not store direct component pointers as long-term state; use behavior IDs, component type handles, component names, and slots as handles that can be validated each frame.

Only add these when `DEVELOPMENT_TRACKING.md` says the next milestone requires them.

---

## When the User Says the Current Milestone Is Done

When the user says something like:

- “M2 is done”
- “we are done with this milestone”
- “move to the next step”

Codex should:

1. Update `DEVELOPMENT_TRACKING.md`:
   - mark the current milestone as done;
   - promote the next milestone to current;
   - add only the folders/files needed for that next milestone.

2. Update this `AGENTS.md`:
   - replace the “Current Coding Milestone” section;
   - update the allowed file/folder list;
   - keep old completed milestone details out of this file unless still needed.

3. Do not expand the repository structure beyond the new milestone.

---

## Development Style

- Baby steps.
- Small tests first.
- Minimal headers.
- No speculative folders.
- No large rewrites without explicit request.
- Prefer clear ownership over clever abstractions.
- Prefer simple data structures until profiling or complexity proves otherwise.

### C++ Formatting Preferences

- Use 4 spaces for indentation.
- Put opening braces on the same line for functions, classes, and control blocks.
- Keep simple one-line guard clauses readable; braces are not required for a single obvious statement.
- Prefer the current project style over adding strict boilerplate everywhere: do not add `nodiscard`, `noexcept`, `const`, or similar qualifiers unless they make the code clearer or solve a real problem.
- For new classes, keep access sections visually simple: indent `private:`/`public:` one level inside the class and indent members one level under the access label.
- Do not reformat unrelated existing code just to normalize style.
