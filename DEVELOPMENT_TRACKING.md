# DEVELOPMENT_TRACKING.md — Liquid Project Tracking

This file tracks the practical development path for the `liquid` engine.

The goal is to keep the current work small enough to implement and test without being overwhelmed.

---

## Project Layers

### Liquid Layer

The final application/research project.

Purpose:

- help neurodivergent people reduce cognitive friction;
- prepare environments for tasks;
- adapt routines to individual preferences;
- collect data through real and simulated interactions.

Liquid Layer is not the current coding target.

---

### Liquid

The standalone engine/framework/runtime.

Purpose:

- provide a deterministic behavior runtime;
- later provide an adaptive layer that can generate or modify behavior blocks;
- be reusable by both the final Liquid Layer application and future simulation/data-collection tools.

The repository currently being planned is `liquid`.

---

### Stage 1 — Solid

Current stage.

Purpose:

- build the deterministic ECS-inspired foundation;
- model behaviors, components, intents, queries, and registries;
- avoid LLM, Lua, hardware, and application-specific logic at first.

Solid should be small, deterministic, testable, and independent.

---

### Stage 2 — Liquid

Future stage.

Purpose:

- add the adaptive/LLM layer;
- generate or modify solid behavior blocks;
- help users express routines in natural language;
- keep the deterministic Solid runtime as the execution authority.

Not active yet.

---

### Stage 3 — Liquid Layer

Future application stage.

Purpose:

- build the neurodivergent-support smart environment project;
- connect real/simulated environments;
- collect user interaction data;
- explore gamified or public testing flows.

Not active yet.

---

## Current Development Policy

Start flat and small.

Do not create the full future architecture upfront. Add folders only when the current milestone needs them.

Current minimal structure:

```text
liquid/
  CMakeLists.txt
  AGENTS.md
  CURRENT_STATE_EVALUATION.md
  DEVELOPMENT_TRACKING.md
  Liquid_Concepts_and_Architecture.md
  ARTICLE_NOTES.md

  include/
    liquid/
      Ids.hpp
      IntentLifetime.hpp
      IntentExpiration.hpp
      ComponentStorage.hpp
      ComponentRegistry.hpp
      world/
        WorldState.hpp
        Coordinator.hpp
        World.hpp
      BehaviorRegistry.hpp
      IntentRegistry.hpp
      SystemRegistry.hpp
      Runtime.hpp
      scripting/

  src/
    ComponentRegistry.cpp
    world/
      Coordinator.cpp
      World.cpp
    BehaviorRegistry.cpp
    IntentRegistry.cpp
    SystemRegistry.cpp
    IntentExpiration.cpp
    Runtime.cpp
    scripting/

  tests/
    test_ids.cpp
    test_component_storage.cpp
    test_component_registry.cpp
    test_world.cpp
    test_behavior_registry.cpp
    test_intent_registry.cpp
    test_system_registry.cpp
    test_intent_expiration.cpp
    test_intent_resolution.cpp
    test_runtime.cpp
    test_lua_behavior.cpp
    test_stress.cpp
```

---

## Stage 1 — Solid Milestones

### M1 — Modified ECS Core

**Status:** Done

Completion note:

M1 is complete. The ECS core now has behavior identity, typed component registration/storage, coordinator-managed behavior signatures, behavior-owned intent pools, template-addressed systems with membership callbacks, expanded assert tests, deterministic stress coverage, and an opt-in sanitizer build mode.

Goal:

Build the smallest core that can create behaviors, create immutable intents owned by behaviors, create shared named component instances, query components by registered type handle and name, track behavior access, and clean up correctly.

Components are shared plain data instances queried by `ComponentType<T>` handles and component names. `ComponentStorage<T>` is the typed slot pool for one component type and owns access records for slots in that storage. `ComponentRegistry` owns component type IDs, the type-erased storage map, and globally unique names per component type.
Behaviors do not own components exclusively; they receive read/write access to named components. Access is stored in the typed storage as `BehaviorId -> vector<ComponentSlotAccess>`, where each access records a slot and read/write mode. Systems see this through `World`/`ComponentRegistry` as `name -> ComponentSlotId` for a behavior and component type.
Component types are registered with explicit stable names in M1 and return typed runtime handles. `Signature` is the shared component-type bitset used by both behavior composition and system requirements. As of M4, `World` owns this state and `Coordinator` performs the cross-manager consistency logic internally.
Packed composite keys were used by the original M1 owner/local intent ID prototype; current intent IDs are global recyclable record handles while component access uses behavior/type/slot indexing.
System coordination is part of the M1 ECS core. `SystemRegistry` registers `System` instances by concrete system type, stores each system's `Signature` and matching behavior membership, and emits membership callbacks when behaviors enter or leave a system. `Coordinator` compares behavior signatures to system signatures and updates membership when access or lifetimes change.

Current implementation notes:

- `ComponentType<T>` is the public typed handle returned from `register_component<T>(...)`.
- `TypeName -> ComponentTypeId` is resolved at registration; `ComponentTypeId` is then the internal key for storages, name maps, and behavior signatures.
- `World` is the outside-facing interface for creating, managing, reading/writing, and deleting behaviors, intents, and components.
- Component removal clears intents targeting the removed slot, clears access before recycling it, erases name/slot indexes, and lets internal coordinator logic reset affected behavior signatures.
- World-created component intents require write access and are removed when the owner loses write access to the target.
- Behavior destruction clears component access maps, erases behavior signatures, destroys owned intents, and recycles the behavior ID.
- System execution is not part of M1. Systems are template-addressed by concrete type and currently have `Signature` requirements, matching behavior membership, and membership-event callbacks.
- `LIQUID_ENABLE_SANITIZERS` is available as an opt-in CMake option for AddressSanitizer and UBSan on Clang/GCC.
- Latest verification: full CMake build and all eight M1 tests pass in `/tmp/liquid-m1-tests`; sanitizer build and all eight tests pass in `/tmp/liquid-m1-sanitizers`.

Future tracking:

- Completed M2 intent lifetime and expiration keeps valid behavior ownership and intent cleanup enforced through the current public world boundary.
- Future resolution systems should consume `name -> ComponentSlotId` maps and resolve component data through coordinator/registry APIs.
- M1 system coordination now covers inherited system objects, `Signature` requirements, behavior-to-system matching, and system membership updates.
- `Coordinator` remains the internal cross-manager logic that updates system membership after behavior signatures change through access grants, access revokes, component removal, or behavior destruction.
- Deterministic system execution order and frame-loop scheduling are future runtime responsibilities after the M1 system registry/interface exists.
- Future runtime loops, systems, and adapters should not keep long-lived raw component pointers; prefer behavior IDs, typed component handles, names, and slots that can be revalidated.

Scope:

- IDs;
- behavior registry;
- intent registry;
- component storage;
- component registry;
- coordinator;
- system registry/interface;
- basic behavior/component query mechanism;
- tests;
- CMake boilerplate.

Files expected:

```text
include/liquid/Ids.hpp
include/liquid/ComponentStorage.hpp
include/liquid/ComponentRegistry.hpp
include/liquid/world/WorldState.hpp
include/liquid/world/Coordinator.hpp
include/liquid/world/World.hpp
include/liquid/BehaviorRegistry.hpp
include/liquid/IntentRegistry.hpp
include/liquid/SystemRegistry.hpp

src/ComponentRegistry.cpp
src/world/Coordinator.cpp
src/world/World.cpp
src/BehaviorRegistry.cpp
src/IntentRegistry.cpp
src/SystemRegistry.cpp

tests/test_component_storage.cpp
tests/test_component_registry.cpp
tests/test_world.cpp
tests/test_behavior_registry.cpp
tests/test_intent_registry.cpp
tests/test_system_registry.cpp
tests/test_ids.cpp
tests/test_stress.cpp
```

Success criteria:

- [x] Create a `BehaviorId`.
- [x] Create an `IntentId` owned by a behavior.
- [x] Create named component instances.
- [x] Retrieve components by type/name.
- [x] Remove components safely.
- [x] Track behavior access as a `name -> ComponentSlotId` map per behavior and component type.
- [x] Enforce read/write access at the `Coordinator` boundary before behavior code receives access.
- [x] Destroy a behavior and clean its owned intents and component access maps.
- [x] Recycle behavior IDs through registry APIs.
- [x] Recycle intent IDs through registry APIs.
- [x] Register systems with signatures.
- [x] Update system membership when behavior signatures change.
- [x] Remove destroyed behaviors from system membership.
- [x] Compile and run all M1 tests.
- [x] Compile and run expanded stress/property-style tests.
- [x] Compile and run all tests with opt-in AddressSanitizer and UBSan.

Out of scope:

- runtime frame loop;
- event queue;
- intent resolution;
- intent expiration systems;
- Lua;
- LLM;
- adapters;
- simulation CLI.

---

### M2 — Intent Lifetime and Expiration

**Status:** Done

Completion note:

M2 adds explicit persistent/until-time intent lifetime metadata, typed immutable intent records, owner and target indexes, stateless expiration helpers, world-facing expiration cleanup, and internal cleanup for invalidated intent targets/write access.

Goal:

Add the first lifetime model for immutable intent records.

Expected concepts:

- intent lifetime metadata;
- until-time expiration;
- persistent intents that do not expire from time;
- cleanup of expired intents.
- explicit cancellation by destroying an intent.

Current implementation:

- Keep intent lifetime as intent metadata, not component storage.
- Keep the public world boundary responsible for behavior-owned intent creation and cleanup.
- Keep `IntentRegistry` as the source of truth for typed intent records, with secondary indexes for owner cleanup and `ComponentTypeId -> ComponentSlotId` target lookup.
- M2 baseline: keep expiration classification outside `IntentRegistry`; the registry stores data and deletes records when asked. M3 supersedes this only for resolution-time cleanup inside `IntentRegistry::resolve(...)`.
- Require current write access when `World` creates component-target intents.
- Destroy coordinator-owned intents when their target component slot is removed or when their owner loses write access to that target.
- Add deterministic expiration logic that receives explicit time/frame input rather than depending on wall-clock globals.
- Classify expired intents without mutating their semantic request contents or registry indexes.
- Destroy expired intents in two phases: collect IDs first, then delete, so cleanup never removes while iterating.
- Cleanup may remove expired intents through the existing registry/coordinator flow.
- Explicit cancellation currently means destroying the intent record; no canceled lifetime state is stored.

M3 supersession note:

- M2 kept expiration classification outside `IntentRegistry`.
- M3 deliberately moves resolution-time expiration deletion into `IntentRegistry::resolve(...)` because resolution mutates registry-owned records and indexes.
- The standalone expiration helpers remain useful for direct cleanup tests and future loop orchestration, but registry-owned resolution is the source of truth for resolving a component type.

Likely new files:

```text
include/liquid/IntentLifetime.hpp
include/liquid/IntentExpiration.hpp
src/IntentExpiration.cpp
tests/test_intent_expiration.cpp
```

Keep structure flat unless there are enough files to justify splitting folders.

Success criteria:

- [x] Represent persistent and until-time intent lifetime metadata.
- [x] Classify whether an intent lifetime is expired from explicit input.
- [x] Evaluate until-time intents deterministically from explicit input.
- [x] Leave persistent intents alive until deletion by higher-level cleanup.
- [x] Clean expired intents without breaking owner-pool recycling.
- [x] Keep coordinator-created component intents valid against target slot lifetime and owner write access.
- [x] Preserve all completed M1 behavior/component/system tests.

---

### M3 — Intent Resolution

**Status:** Done

Completion note:

M3 adds registry-owned intent resolution with explicit `IntentPriority`, resolution-time expiration cleanup inside `IntentRegistry::resolve(...)`, deterministic highest-priority/highest-ID selection, and `ComponentName -> IntentId` output for one component type at a time. It does not add a frame loop, component mutation, adapter commands, or runtime execution.

Goal:

Clean expired intents and resolve remaining intent candidates into selected intent handles.

Expected concepts:

- intent target;
- intent value;
- intent priority;
- component-name to slot maps for competing intents;
- future merge policy for compatible intents;
- intent source metadata;
- deterministic registry selection policy;
- selected intent handle output.

Current implementation direction:

- `IntentRegistry::resolve(...)` collects expired intent IDs and destroys them before selection when explicit time input is provided.
- Resolve one component type at a time from a prepared `ComponentName -> ComponentSlotId` map.
- Produce `ComponentName -> IntentId` selections; do not mutate components, run a frame loop, or emit adapter commands in M3.
- Keep registry selection deterministic: highest priority wins, then highest `IntentId`.
- Keep component-specific merge as future work via external traits/free functions, not functions stored in component data.

Likely new files:

```text
tests/test_intent_resolution.cpp
```

---

### M4 — Minimal Frame Loop

**Status:** Done

Completion note:

M4 adds `World` as the public state boundary, `WorldState` as the owner of world-local registries/signatures, internal coordinator logic over that state, and `Runtime` as the sole frame-phase driver. Frames use explicit nondecreasing time, run systems in deterministic registration order before resolving their intents, and retain a small success or failure log. The World layer now lives under dedicated `world` folders.

Goal:

Introduce a minimal deterministic loop that calls the existing managers/systems in order.

Expected concepts:

- frame number;
- frame context;
- begin frame;
- run intent expiration;
- run systems;
- resolve intents;
- cleanup;
- frame log stub.

This is the first milestone where a `runtime/` folder may become justified.

Current implementation direction:

- Keep the loop deterministic and explicit; no wall-clock globals.
- `World` owns `WorldState`, enforces cross-registry invariants, and exposes domain commands and queries.
- `Runtime` owns a `World` and is the only caller of frame-phase operations.
- Keep `Coordinator` as internal consistency logic for behavior signatures, permissions, cleanup, system membership, and registry forwarding.
- Execute `begin -> expire -> systems -> resolve -> end`; intents created by a system participate in that same frame's explicit resolution requests.
- Run systems through `SystemRegistry` in deterministic registration order with `System::run(World&, FrameNumber, IntentTime)` while world and system topology is frozen.
- Accept equal timestamps but reject decreasing `IntentTime` before a frame starts.
- Treat an escaping phase exception as fail-stop: preserve the partial frame log, do not advance the frame number, and reject further frames on that `Runtime` instance.
- Do not add adapters, events, Lua, LLM integration, simulation CLI, or physical-world application.

Likely new files:

```text
include/liquid/Runtime.hpp
include/liquid/world/WorldState.hpp
include/liquid/world/World.hpp
src/Runtime.cpp
src/world/World.cpp
tests/test_runtime.cpp
tests/test_world.cpp
```

---

### M5 — Lua Behavior Scripting

**Status:** Done

Goal:

Allow Lua behaviors to create intents through controlled APIs.

Important rule:

Lua creates new intents. It does not mutate existing intents.

Implemented boundary:

- Pin the official Lua C API at Lua 5.4.8 for M5.
- Give Lua only typed, named, allowlisted intent-creation capabilities. Never expose `World`, `Coordinator`, registries, storage, raw component slots, component pointers, or owner selection.
- Cache only immutable host capability descriptions by lifecycle-unique world and behavior access revisions. Build a fresh Lua state, capability table, and copied component snapshot for every execution.
- Fix the executing `BehaviorId` and current time in the host. Lua may choose only persistent lifetime or a checked duration in monotonic session-relative milliseconds.
- Execute scripts in protected mode with explicit instruction, Lua-memory, source-size, diagnostic-size, and per-execution created-intent limits. Do not use `luaL_openlibs`; load only the libraries deliberately approved for behavior code.
- Keep `pcall`, `xpcall`, coroutine, dynamic loading, package, OS, I/O, debug, metatable, and raw-table authority out of the script environment. Omit native string pattern/format functions that could bypass the VM instruction hook or reveal object addresses.
- Catch C++ exceptions inside every Lua C closure and avoid Lua long jumps across live C++ RAII objects.
- Let a writable capability buffer multiple `propose(...)` requests, including multiple requests for the same component. Commit only after successful script completion; if a later commit fails, roll back only the intent IDs already created by that execution.
- Keep existing intent ownership, permission, priority, lifetime, cleanup, and resolution rules as the only authority behind the binding.
- Treat `Liquid_Concepts_and_Architecture.md`, section 13, as the canonical script-authoring and future model-prompt contract. A future prompt builder must combine that stable contract with current behavior permissions and trusted machine-readable schema metadata registered beside each codec; the current executable codec functions are not introspectable.
- Keep adapters, LLM integration, simulation CLI, MQTT, voice, and final Liquid Layer application concepts out of scope.

M5 files:

```text
include/liquid/scripting/
src/scripting/
tests/test_lua_behavior.cpp
```

---

### M6 — Simulation CLI

**Status:** Done

Goal:

Add a small executable that runs Solid without hardware.

This is the first milestone where `apps/` becomes useful.

Implemented:

- Add one small executable under `apps/` that links the existing `liquid` library.
- Run reproducible scenarios with explicit frame times and deterministic inputs.
- Accept simulated initial state and Lua behavior source without requiring physical adapters.
- Report frame results, selected intents, and bounded script errors in a form suitable for inspection and golden tests.
- Keep events, physical adapters, LLM integration, MQTT, voice, and final Liquid Layer application behavior out of scope.

Completion note:

- Added `liquid_sim_cli` as a deterministic, hardware-free executable over the existing Runtime and Lua boundary.
- Added strict scenario parsing, bounded script input and diagnostics, stable line-oriented output, and explicit exit statuses.
- Added successful, rollback-on-error, fresh-process replay, fixed-seed stress, strict-warning, and sanitizer-backed regressions.
- Documented the research-backed scenario contract and its limits in `M6_TEST_BASE.md`.

Done when:

- a user can run a minimal Solid scenario from the command line without hardware;
- the scenario exercises the existing Runtime and M5 Lua boundary rather than introducing a second execution path;
- repeated runs with the same inputs produce the same observable result;
- at least one end-to-end CLI regression covers successful execution and one covers a bounded script error.

---

## Stage 2 — Liquid Milestones

**Status:** Planning

No Stage 2 implementation milestone is current yet. The next task is to define and approve the first minimal Liquid milestone before adding source folders or implementation code.

Expected future areas:

- LLM-assisted behavior generation;
- behavior proposal flow;
- user confirmation;
- adaptation based on logs/preferences;
- no LLM authority inside deterministic conflict resolution by default.

---

## Stage 3 — Liquid Layer Milestones

Not detailed yet.

Expected future areas:

- neurodivergent-support scenarios;
- environment preparation routines;
- gamified/simulated data collection;
- consent/privacy/data export design;
- real smart-home adapters.

---

## Milestone Advancement Rule

When a milestone is completed:

1. Mark its status as `Done`.
2. Add a short completion note.
3. Promote the next milestone to `Current`.
4. Expand only the new current milestone.
5. Update `AGENTS.md` so Codex knows the new allowed scope.
6. Do not expand future stages unless the user explicitly asks.

---

## Current Notes

- Stage 1 Solid is complete through M6.
- The current focus is defining and approving the first minimal Stage 2 Liquid milestone; implementation scope is not yet authorized.
- M1 modified ECS core is complete and should be treated as foundation, not active scope.
- M2 now uses typed intent-record storage with owner and component-target indexes; `IntentId` no longer encodes the owner behavior.
- M3 registry-owned intent resolution is complete and should be treated as foundation, not active scope.
- M4 is complete: it introduces the smallest deterministic frame loop, `World` as public state boundary, explicit frame input, expiration, intent resolution requests, registration-ordered system execution, and a small frame log.
- M5 is complete: it provides the controlled Lua behavior scripting boundary needed for behaviors to create intents without exposing runtime internals.
- M6 is complete: the deterministic, hardware-free CLI exercises the existing Runtime and Lua boundary with inspectable output and end-to-end regressions.
- The project owner will implement core logic manually.
- Codex should generate headers, tests, CMake, and boilerplate unless explicitly asked to implement logic.
- The current structure is intentionally small to keep the project controllable.
- Superposition is cloned at `/home/raul/Desktop/superposition` and should be used as the persistent ECS reference.
- For component-manager work, keep the distinction clear: component type is the system query signature; component names are the user/device-facing way to find shared component instances.
- Prefer registered component IDs for type lookup, while using string names for user-created component instances.
- Access relationships carry read/write permissions and are exposed as `name -> ComponentSlotId` maps for each behavior and component type.
- World-created component intents require current write access, and internal coordinator logic removes them when target slots or owner write access become invalid.
- Use named constants for packed key shifts/masks instead of repeating raw numeric literals when a packed key is still useful.
- Intents are not component rows. They are immutable proposals to change component state or emit effects.
- Intent lifetime is metadata on immutable intent records, not component storage.
- Script components and systems that execute them are future work; the completed M1 storage/query model only makes that path possible later.
