# Liquid Concepts and Architecture v0.3

**Status:** Living baseline during Solid v0.1 finalization
**Scope:** Conceptual architecture, vocabulary, and accepted design direction  
**Project:** Liquid Layer  
**Engine / framework:** Liquid  
**Current development stage:** Solid v0.1, S0-S7 finalization
**Date:** August 2026

---

## 1. Purpose

This document defines the conceptual baseline for the Liquid project as implementation progresses. It captures the current architectural direction, vocabulary, stage names, and design boundaries agreed so far.

This is not the coding guide. The coding guide will define C++ conventions, header structure, file responsibilities, call order, tests, and implementation-level contracts.

---

## 2. Naming and Stages

The project is divided into three conceptual layers.

| Name | Meaning | Current status |
|---|---|---|
| **Liquid Layer** | Final application/research project focused on adaptive smart environments for neurodivergent people. | Future project layer |
| **Liquid** | Standalone engine/framework/runtime used by Liquid Layer and by simulation/data-collection tools. | Engine repo |
| **Solid** | First implementation stage of Liquid: deterministic ECS-inspired runtime. | M1-M5 complete; M6 current |
| **Liquid stage** | Later implementation stage: adaptive/LLM layer that creates, modifies, and explains Solid blocks. | Future focus |

The internal code should not overuse metaphorical names. Folder and file names should describe responsibility: `vocabulary`, `core`, `runtime`, `events`, `behaviors`, `intents`, `systems`, and `adapters`.

---

## 3. Architectural Thesis

Liquid separates adaptive reasoning from deterministic execution.

> Liquid uses probabilistic models to discover, negotiate, and synthesize behavior, but uses a deterministic ECS-inspired runtime to execute, constrain, audit, and reproduce behavior.

The key metaphor is:

- **Solid:** behavior blocks that are explicit, deterministic, inspectable, testable, and replayable.
- **Liquid:** adaptive reasoning that molds those blocks around the user's context, preferences, and routines.
- **Liquid Layer:** the user-facing system where the liquid layer shapes solid behavior to reduce cognitive friction.

The LLM should not be the authority that directly controls the environment every frame. It should help infer context, propose behavior, generate scripts, explain decisions, and adapt preferences. Once behavior becomes active, it should be represented as Solid runtime data and handled by deterministic systems.

---

## 4. Why This Architecture Exists

Liquid is motivated by two problems that pull in opposite directions.

First, smart environments need reliability. Physical actions close to the user must be predictable, auditable, and constrained. Background behaviors should not unexpectedly override explicit user intent, and actions should be traceable to their source.

Second, neurodivergent support often depends on ambiguous personal context. Difficulty starting tasks, hyperfocus, transition friction, sensory sensitivity, sleep irregularity, and routine instability are not solved well by fixed generic rules. The system needs adaptive interpretation and personalization.

The architecture therefore uses a rigid deterministic runtime underneath and a flexible adaptive layer above it.

---

## 5. Stage 1: Solid

Solid is the first implementation stage. Its goal is to build the standalone deterministic engine that later systems will use.

Solid includes:

- a minimal custom ECS-inspired core;
- behaviors as the main domain object instead of generic entities;
- components as plain data;
- systems as logic over component sets;
- intents as first-class runtime objects;
- intent lifetime modeled through intent data and factories;
- deterministic frame execution;
- event input and resolved effect output;
- logs and replay support from the beginning;
- simulation-friendly adapters.

Solid excludes for now:

- LLM behavior generation;
- voice input;
- custom wake words;
- neurodivergence-specific application routines;
- production smart-home hardware integration;
- fine-tuning.

Solid should be usable as a standalone framework for simulation and testing before Liquid Layer exists.

---

## 6. Stage 2: Liquid

Liquid is the adaptive layer added on top of Solid.

Liquid may include:

- agents that interpret user requests;
- generation of behavior proposals;
- translation of natural language into Solid runtime objects;
- adaptation of preferences and thresholds;
- explanation of why behaviors were created or resolved;
- optional async script/LLM checks;
- future data-driven personalization.

Liquid must respect Solid's constraints:

- agents do not mutate world state directly;
- agents propose intents or behavior proposals;
- generated behavior must be validated before activation;
- long-running model calls are asynchronous;
- deterministic runtime remains the final authority for execution and conflict resolution.

---

## 7. Stage 3: Liquid Layer

Liquid Layer is the final application/research system focused on neurodivergent support.

Its goal is to reduce cognitive friction by preparing environments, lowering initiation cost, supporting transitions, and adapting to individual routines. Examples include:

- preparing a study environment;
- soft interruption during hyperfocus;
- sensory-aware lighting and sound changes;
- sleep and routine support;
- task-start support;
- gentle reminders that account for context.

Liquid Layer should be built on top of Liquid, not mixed into the engine. The engine should remain generic enough to run simulations, games, data-collection tools, and real smart-environment adapters.

---

## 8. Core Concepts

### Behavior

A behavior is a runtime object that represents something the system may do over time. Behaviors replace the generic ECS word "entity" in the project vocabulary.

A behavior may be persistent, temporary, scheduled, manually triggered, or generated later by an agent. Behaviors do not directly mutate external devices. They hold component state and are the owner/source for intents.

### Agent

An agent is a behavior-like actor driven by adaptive reasoning. Agents live conceptually beside behaviors, but they follow stricter boundaries. They may propose intents, propose new behaviors, or ask for confirmation. They do not directly mutate components or bypass registry-owned intent resolution.

### Component

A component is plain engine-side data. Components may contain physical adapter references such as a device string, but they should not contain behavior logic, virtual methods, or hidden ownership rules. Logic belongs in systems.

The component type is the signature that systems query for. A user/device-facing component name identifies a concrete shared component instance inside that component type.

Behaviors do not own components exclusively. Multiple behaviors may have read/write access to the same named component through access records.

### System

A system is logic that processes matching component sets during a frame. Systems are responsible for updating lifetimes, resolving intents, advancing behavior runs, applying events, cleanup, and producing effects.

For example, a future script system would process behaviors with script-related components and create intents from those behavior owners.

### Intent

An intent is a proposed component-state change or external effect. It is not immediately applied. Intents are produced by behaviors, agents, systems, events, or schedules; `IntentRegistry` resolves selected intent handles for component targets.

Intents are not components. They are immutable records with owner/source, target, value or operation, priority, lifetime metadata, and merge/conflict policy data.

Examples:

- set a light brightness;
- send a notification;
- reserve an audio output;
- request a simulated user prompt;
- create a future behavior proposal.

### Lifetime

Intent lifetime is modeled as intent data, not as component storage.

M2 implements persistent and until-time lifetimes. Until-frame, future cancellation events, and script-based lifetime data remain future policies. M3 resolution-time cleanup happens inside `IntentRegistry::resolve(...)` because it deletes registry-owned records and updates registry-owned indexes.

Normal code should use factories/bundles to create valid intent records and avoid missing required fields.

### Resolved Effect

A resolved effect is the future result of applying a selected intent. Current M3 resolution returns `ComponentName -> IntentId`; later runtime/application work will turn selected handles into component changes or adapter-facing effects.

### Command / Adapter Action

A command is the adapter-facing operation produced from a resolved effect. The adapter translates this command into an external action such as MQTT publish, CLI output, simulation state update, or future UI/voice behavior.

### Frame

A frame is one deterministic execution step of the runtime. In the current Solid core, `Runtime` begins the frame, expires old intents, runs systems, resolves explicit intent requests, records completion, and advances the frame number. Future input, event, effect, and adapter phases must be added around this fixed ownership boundary rather than bypassing it.

---

## 9. Intent Resolution Model

The current conceptual flow is:

```text
Behavior / Agent components
    -> processed by systems, events, or schedules
    -> creates an Intent owned by a behavior or agent
    -> Intent exists as immutable runtime data
    -> IntentRegistry removes expired intents during resolution
    -> IntentRegistry resolves active intents by component name and target slot
    -> Accepted intents change component state or produce effects
    -> ResolvedEffect is produced
    -> Adapter turns effect into external command/action
```

Important rules:

- Intent resolution does not ask each behavior what it wants every frame.
- Systems, events, schedules, or agents add intents to the world, usually on behalf of a behavior or agent owner.
- Active intents remain until lifetime metadata marks them expired or cleanup removes them.
- Intent queues may be organized by target key, with priority per target so conflict and merge checks stay local to the affected component or effect.
- Explicit user intent should normally outrank background behaviors.
- LLM-based arbitration is not a default conflict policy.
- If an LLM or script check is slow, it should run asynchronously and report back through events.

---

## 10. Component and Intent Data Design

Accepted baseline:

- no C++ inheritance between components;
- components remain plain data;
- component storage is only a typed slot pool for one component type;
- `ComponentRegistry` maps template component types plus global component names to storage slots;
- behavior queries start from a component type, so storage itself does not need to know its component type;
- behavior state, capabilities, access, and system eligibility are represented through ordinary shared named component instances plus access records;
- access relationships connect a behavior to a named component with read/write permissions;
- behavior-facing queries expose `name -> ComponentSlotId`, where the name is the user/device-facing key and the slot is the typed storage handle;
- `World` is the public state boundary and owns the world-local state;
- `Coordinator` contains the internal cross-manager behavior permission, cleanup, and signature logic;
- trusted component resolution uses the registered component type handle plus the slot after coordinator validation;
- intents are immutable proposal records, not component rows;
- intent target keys should identify the component instance or external effect being changed;
- intent queues can use per-target priority so registry resolution can inspect the strongest candidate and future merge logic can combine compatible proposals;
- use factories or bundles so required component sets and intent fields are readable and hard to misuse;
- use validation systems to detect invalid behavior component combinations and invalid intent records.

Example conceptual composition:

```text
Shared component instances:
  LightState "officeLight"
    physicalDevice: "zigbee://office-light"
    brightness: 40

Behavior access:
  lightTracking -> "officeLight", LightState, ReadWrite
  focusActive -> "officeLight", LightState, Read

Intent record:
  owner BehaviorId
  target key
  operation/value
  priority
  lifetime metadata
  merge/conflict policy
```

The scheduler/runtime should not need to know every intent policy detail. `IntentRegistry` owns resolution-time cleanup and selected-handle resolution; later runtime work applies accepted selections deterministically.

---

## 11. Repository and Folder Direction

Current intended engine repo:

```text
liquid/
  CMakeLists.txt
  README.md

  include/
    liquid/
      vocabulary/
      core/
      runtime/
      events/
      behaviors/
      intents/
      systems/
      adapters/

  src/
    core/
    runtime/
    events/
    behaviors/
    intents/
    systems/
    adapters/

  apps/
    liquid_sim_cli/

  tests/
    vocabulary/
    core/
    runtime/
    intents/
    systems/

  docs/
    design/
    adr/
```

The `include/liquid/` prefix is used because Liquid is intended to be a reusable engine/framework, not only a private app. Public include paths should avoid generic names such as `<core/World.hpp>` or `<events/Event.hpp>`.

Folder responsibility:

| Folder | Responsibility |
|---|---|
| `vocabulary/` | Shared project language: IDs, time, priority, result types, names. |
| `core/` | Minimal ECS-inspired world, component storage, behavior identity, queries. |
| `runtime/` | Frame context, frame loop, runtime orchestration. |
| `events/` | Events entering the runtime and event queue/log definitions. |
| `behaviors/` | Behavior metadata, behavior components, behavior factories. |
| `intents/` | Intent records, queues, factories/bundles, resolution policy, resolved effects. |
| `systems/` | Systems that run over world data during frames. |
| `adapters/` | External boundary: CLI, simulation, MQTT, future web/voice. |

---

## 12. Accepted Decisions So Far

- Liquid Layer is the final application/research project.
- Liquid is the standalone engine/framework/runtime.
- Solid is the first stage: deterministic ECS-inspired runtime.
- The engine should be its own repo, reusable by both the final application and future simulation/data-collection environments.
- The internal folders should use technical names, not the metaphors `solid` and `liquid`.
- We will build a minimal custom ECS instead of starting with an ECS library.
- The domain term is behavior, not entity, where possible.
- M1 uses simple world-local `BehaviorId` and `IntentId` handles.
- ID recycling is allowed through registry APIs in M1.
- Public behavior, intent, component, system, and related runtime handles are world-bound generational handles in v0.1; stale and cross-world handles are rejected and exhausted generations retire their slots.
- Composite lookup keys may pack two 16-bit handles into one 32-bit value when that makes ownership or target lookup simpler and deterministic.
- `IntentId` is now a global recyclable handle to an intent record; owner and target are stored on the record and indexed separately.
- Component intent targets use `ComponentTypeId + ComponentSlotId` and are indexed as type -> slot -> intent IDs.
- Type-specific intent records extend the common intent data with a component replacement value, without making intents into component rows.
- Components should be plain data.
- Component type is the system query signature; component names are the stable user/device-facing keys for shared component instances.
- Access relationships hold permissions between behaviors and named component instances.
- `ComponentType<T>` is the typed runtime handle returned by explicit component registration.
- Component type lookup is template-driven in M1, following the Superposition-style manager flow.
- `Runtime` owns the minimal deterministic frame loop, is the sole frame-phase driver, and advances a `World` using nondecreasing explicit time.
- `World` owns `WorldState`: component, behavior, intent, and system registries plus behavior signatures. It exposes domain commands and queries but not direct system-execution, expiration-cleanup, or intent-resolution phases.
- World-layer headers and sources live under a dedicated `world` folder because `World` is the public state boundary, while `Runtime` remains separate frame orchestration.
- `Coordinator` operates internally on `WorldState` for behavior existence checks, cross-manager permission checks, cleanup sequencing, registry forwarding, and system membership.
- Systems are registered by concrete type and store `Signature` requirements, behavior membership, and membership callbacks.
- A system's initial `Signature` is supplied atomically at registration. `Coordinator` alone derives membership from signatures; the public `World` API does not provide manual membership overrides.
- Membership callbacks are observational and should not throw or mutate topology. Membership transitions are committed before the first callback error is rethrown; callers must inspect state rather than assume an exception implies rollback.
- M4 runs systems in deterministic registration order with `System::run(World&, FrameNumber, IntentTime)` before resolution, so system-created intents can be selected in the same frame.
- Component, behavior, permission, and system topology is frozen during system dispatch; systems may use permitted component data and create or cancel intents.
- A frame that lets an exception escape is not committed: its number does not advance, its partial log records the failed phase, and that `Runtime` becomes fail-stop because world mutations are not transactional yet.
- Systems should own behavior logic.
- Intents are immutable proposed component-state changes or effects, not component rows and not immediate actions.
- Intent owner pools are aligned with behavior creation/destruction through `World`.
- World-created component intents require write access and are cleaned up when their target slot or owner write access becomes invalid.
- The completed M1 test suite uses assert binaries, deterministic stress coverage, and an opt-in AddressSanitizer/UBSan CMake mode.
- Intent resolution should be target-oriented; current M3 resolves one component type from `ComponentName -> ComponentSlotId` into `ComponentName -> IntentId`.
- A selected intent handle later becomes a resolved effect, then an adapter command/action.
- Completed M2 intent lifetime is persistent or until-time; future factories/bundles should create valid intent records without missing required metadata.
- `IntentTime` means monotonic milliseconds since the runtime session began. It is not epoch or wall-clock time.
- Component pointers and references are borrowed views valid only until the next structural mutation of that typed storage; they are never retained across frames or exposed to Lua.
- `World` and `Runtime` are single-thread-confined and require external synchronization if driven from more than one thread.
- M5 Lua receives a narrow capability API: typed component name and value, host-fixed behavior owner and current time, and persistent or checked-duration lifetime. Scripts never receive `World`, coordinator/registry/storage objects, raw slots, component pointers, or owner selection.
- Capability layouts cache only immutable host descriptions keyed by lifecycle-unique world and behavior access revisions. Every execution creates a fresh Lua state, fresh access tables, and copied component snapshots, so script mutations cannot retain or enlarge authority.
- Writable capabilities expose only `propose(request)`. A script may buffer multiple requests for the same component, such as a temporary high-priority intent plus a persistent lower-priority fallback; access-table fields are never interpreted as authority.
- Every Lua execution is protected by instruction, Lua-memory, buffered-host-value, string, table, source, diagnostic, and created-intent limits. Proposals commit only after successful execution; partial commit failure rolls back only the intents created by that execution, and errors do not escape `System::run`.
- The script environment omits protected-call and coroutine facilities as well as dynamic loading, package, OS, I/O, debug, raw-table, and metatable authority. This prevents hook errors from being caught in an unbounded loop and keeps the capability table as the only effect boundary.
- Lua host closures catch C++ exceptions at the C boundary and avoid Lua long jumps across live C++ RAII objects.
- No C++ inheritance between components in v1.
- LLM conflict resolution is not a default mechanism.
- LLM/script work should be async when it may be slow.
- Replay/logging must influence the design from the start.

---

## 13. Lua Behavior Scripting and Model Prompt Contract

This section is the canonical authoring contract for M5 behavior scripts. It is intended for human authors, tests, and future prompts that ask a model to generate Lua. If this section and an example disagree, this section wins.

### 13.1 Host integration model

Lua is embedded through the Lua 5.4.8 C API. A `LuaBehaviorRunner` receives a `World`, the host-selected `BehaviorId`, the current monotonic `IntentTime`, and text source. The C++ host registers each script-visible component type with a `LuaComponentCodec<T>`:

```cpp
struct Light {
    int brightness = 0;
};

LuaComponentCodec<Light> lightCodec{
    [](const Light& light) {
        return LuaValue::Table{
            {"brightness", LuaValue{light.brightness}}
        };
    },
    [](const LuaValue& value) {
        const auto& table = value.as_table();
        if (table.size() != 1 || !table.contains("brightness"))
            throw std::runtime_error("Light requires exactly brightness");

        std::int64_t brightness = table.at("brightness").as_integer();
        if (brightness < 0 || brightness > 100)
            throw std::runtime_error("brightness must be between 0 and 100");

        return Light{static_cast<int>(brightness)};
    }
};

LuaBehaviorRunner runner;
runner.expose_component(lightType, "Light", lightCodec);

LuaExecutionResult result = runner.execute(
    world,
    behavior,
    now,
    source
);
```

The codec is part of the security and validation boundary:

- `encode` defines the exact copied value Lua may inspect;
- `decode` defines the exact value shape and ranges Lua may request;
- the script never receives the original component object or a retained pointer;
- exposing a component type does not grant access by itself; the executing behavior must also have current access to each named component instance.

Bindings are frozen after the runner's first execution. Register all component codecs before running scripts.

### 13.2 Execution environment

Every execution creates a fresh `lua_State`, a fresh `_ENV`, fresh access tables, and fresh copied component snapshots. Globals and Lua references do not persist between executions.

The script receives these host globals:

| Global | Meaning |
|---|---|
| `access` | Capability table described below. This is the only effect boundary. |
| `now_ms` | Host-supplied monotonic session time as a Lua integer. It is not wall-clock or epoch time. |

The allowlisted base functions are:

```text
assert  error  ipairs  next  pairs  select  tonumber  type
```

The `table`, `string`, `math`, and `utf8` libraries are available with these removals:

```text
string.dump
string.find
string.format
string.gmatch
string.gsub
string.match
math.random
math.randomseed
```

The string pattern functions run inside native C and cannot be interrupted by the Lua VM instruction hook, so they are intentionally unavailable. `string.format` and base `tostring` are unavailable because they can expose process/object addresses.

The following facilities are not available:

```text
_G                 collectgarbage      coroutine
debug              dofile              getmetatable
io                 load                loadfile
os                 package             pcall
print              rawequal            rawget
rawset             require             setmetatable
tostring           warn                xpcall
```

`pcall` and `xpcall` are deliberately absent. If scripts could catch the count-hook error, a hostile loop could repeatedly catch its instruction-limit failure and continue forever.

Scripts are loaded in text-only mode. Binary Lua chunks and sources containing embedded NUL bytes are rejected.

### 13.3 Capability table shape

The conceptual table path is:

```lua
access.<registered-type-name>.<component-name>
```

For example:

```lua
access.Light.officeLight
```

Dot notation is valid only when both registered names are valid Lua identifiers. A dynamic manifest must provide an already escaped bracket expression for other names:

```lua
access["Lighting Device"]["office-light"]
```

The model must copy the exact path expression from the manifest instead of reconstructing or normalizing component names.

The fields present depend on the behavior's current permission:

| Permission | `value` | `propose` |
|---|---:|---:|
| Read | present | absent |
| Write | absent | present |
| ReadWrite | present | present |

A read/write entry can therefore look like:

```lua
access = {
    Light = {
        officeLight = {
            value = {
                brightness = 10
            },
            propose = function(request)
                -- Host C++ closure.
            end
        }
    }
}
```

The visible table is data, not authority. A script may modify it, replace it, move `propose` to another table, or add fake fields, but none of those operations change the closure's host-bound target or permission. The host never reads fields such as `owner`, `slot`, `type`, or `writable` from Lua.

The `value` field is a snapshot. Mutating it changes only the current Lua table:

```lua
local light = access.Light.officeLight
light.value.brightness = 50
```

This does not write the component and does not create an intent. The script must explicitly call `propose` to request an effect.

### 13.4 Proposal request schema

Call `propose` with dot syntax and exactly one table argument:

```lua
access.Light.officeLight.propose({
    value = { brightness = 50 },
    priority = "high",
    duration_ms = 500
})
```

Do not use method/colon syntax:

```lua
-- Invalid: this passes the component table as an extra first argument.
access.Light.officeLight:propose({
    value = { brightness = 50 }
})
```

The request accepts exactly these fields:

| Field | Required | Contract |
|---|---:|---|
| `value` | yes | Must match the registered component codec exactly. `nil` is not a component value. |
| `priority` | no | `"low"`, `"medium"`, or `"high"`; defaults to `"medium"`. |
| `lifetime` | no | The only accepted value is `"persistent"`. |
| `duration_ms` | no | Nonnegative integer duration relative to host `now_ms`. |

Lifetime rules:

- omit both `lifetime` and `duration_ms` for a persistent intent;
- use `lifetime = "persistent"` to state persistence explicitly;
- use `duration_ms = N` for an until-time intent ending at `now_ms + N`;
- do not provide both `lifetime` and `duration_ms`;
- `duration_ms = 0` is valid;
- the deadline must fit the signed Lua-integer time domain.

Unknown request fields are rejected. `propose` returns no meaningful value.

### 13.5 Lua values accepted by codecs

The boundary transports these value kinds:

```text
boolean
integer
finite floating-point number
string
array table
object table with string keys
```

Constraints:

- NaN and positive/negative infinity are rejected;
- object keys must be strings;
- arrays use contiguous integer keys starting at `1`;
- a table cannot mix object and array keys;
- cyclic table references are rejected;
- shared table aliases are copied as independent values and remain subject to the aggregate entry and buffered-byte limits;
- functions, userdata, threads, and `nil` inside a component value are rejected;
- the component codec may impose stricter fields, types, string rules, enums, and numeric ranges.

Host-encoded arrays carry a private marker so an empty array snapshot remains an array when proposed unchanged. Scripts cannot inspect or forge that marker because metatable and raw-table authority is absent.

A literal empty Lua table, `{}`, is interpreted as an empty object because Lua itself does not distinguish empty arrays from empty objects. A script can create a nonempty array with `{value1, value2}`. If a codec needs an empty array, the script must reuse or clear a host-provided array snapshot; a future array-construction helper may be added if generation without a snapshot becomes necessary.

### 13.6 Buffering, commit, and rollback

Calling `propose` validates and buffers a typed C++ request. It does not mutate the `World` and does not create an intent while Lua is running.

A script may call `propose` more than once for the same component:

```lua
local light = access.Light.officeLight

light.propose({
    value = { brightness = 100 },
    priority = "high",
    duration_ms = 5
})

light.propose({
    value = { brightness = 30 },
    priority = "low",
    lifetime = "persistent"
})
```

This represents a temporary high-priority state with a persistent lower-priority fallback. The normal immutable-intent resolver decides which intent wins.

After successful script execution, the host closes the Lua state, then commits buffered proposals. Immediately before each commit it:

1. uses the host-fixed `BehaviorId`;
2. resolves the host-bound component name to its current slot;
3. verifies that the target still exists;
4. verifies current write permission;
5. creates a new immutable intent through `World`.

If the script, instruction hook, allocator, codec, or callback fails, no buffered proposal is committed. If a later commit fails after earlier proposals from this execution were created, only those newly created intent IDs are destroyed in reverse order. Pre-existing intents remain untouched.

Lua cannot inspect, modify, or delete an existing intent.

### 13.7 Default execution limits

The current defaults are host-configurable through `LuaExecutionLimits`:

| Limit | Default |
|---|---:|
| Source bytes | 64 KiB |
| Lua allocator memory | 8 MiB |
| VM instructions | 100,000 |
| Diagnostic bytes | 4 KiB |
| Intents created per execution | 64 |
| Table depth | 16 |
| Table entries per transported value | 4,096 |
| String bytes | 64 KiB |
| Buffered host value bytes | 8 MiB |

Generated scripts should stay comfortably below these limits instead of attempting to consume the full allowance. Native APIs that could perform unbounded work outside the VM instruction counter are not exposed.

### 13.8 Script-authoring rules for a model

A future model-generation prompt must contain two separate parts.

The first is the stable contract from this section. The second is a dynamic capability manifest generated by trusted host code for the specific behavior and execution context. At minimum, that manifest must state:

- the exact access path for every available component;
- whether the path is readable, writable, or read/write;
- the exact `value` schema expected by that component's codec;
- field types, allowed enums, required fields, and numeric/string ranges;
- the current copied value for readable capabilities;
- current monotonic `now_ms` when time affects the requested behavior.

Example prompt manifest:

```text
Execution time:
  now_ms: 100

Available capabilities:
  - path: access.Light.officeLight
    permission: read_write
    readable value: { brightness = 10 }
    writable schema:
      value must be exactly { brightness = INTEGER }
      brightness range: 0..100

  - path: access.Light.hallLight
    permission: read
    readable value: { brightness = 40 }
    writable schema: none
```

The capability manifest is future integration work; M5 does not yet implement an LLM prompt builder or machine-readable codec schema metadata. `LuaComponentCodec<T>` currently contains executable `encode` and `decode` functions, which cannot be introspected to recover field requirements and ranges. A later integration must register a trusted schema description beside each codec, test that description against codec validation, and combine it with current behavior permissions. The model must never infer authority or schema rules from a snapshot alone.

When generating a script, the model must follow this checklist:

1. Use only capability paths explicitly present in the manifest.
2. Read only entries whose manifest says they contain `value`.
3. Call `propose` only on entries marked writable.
4. Use dot-call syntax with one request table.
5. Match the codec's value schema exactly; do not add explanatory metadata to the request or component value.
6. Use only `low`, `medium`, or `high` priority.
7. Use either persistent lifetime or a nonnegative duration, never both.
8. Do not assume a snapshot mutation changes the world.
9. Do not invent globals, libraries, component types, component names, fields, owner IDs, slots, or existing intent IDs.
10. Produce only Lua source when the caller requests an executable script; do not wrap it in Markdown unless the caller explicitly asks.

The host receives one of these execution statuses:

```text
Success
InvalidBehavior
SourceLimitExceeded
SyntaxError
RuntimeError
InstructionLimitExceeded
MemoryLimitExceeded
IntentLimitExceeded
InvalidProposal
CommitFailed
HostError
```

A future generation pipeline may use the bounded diagnostic from `SyntaxError`, `RuntimeError`, or `InvalidProposal` in a repair prompt, but it must reuse the same capability manifest and must not grant additional authority to make a failed script pass. Retry count and model-selection policy remain future work. `HostError` and `CommitFailed` can represent host-state failures rather than a script-generation error and should not automatically be treated as model-correctable.

Recommended model prompt skeleton:

```text
You generate one Lua 5.4 behavior script for the Liquid runtime.

Follow the Liquid Lua Behavior Scripting and Model Prompt Contract exactly.
Use only the capabilities and schemas in the manifest below.
Do not invent component paths, fields, globals, libraries, IDs, or permissions.
Changing a .value table changes only a local snapshot; call .propose({...}) to request an intent.
Use dot syntax for propose, exactly one request table, and no unknown request fields.
Return only Lua source without Markdown fences.

<insert dynamic capability manifest>

Desired behavior:
<insert user/system behavior requirement>
```

### 13.9 Complete example

Given a manifest declaring `access.Light.officeLight` read/write with `brightness` in `0..100`, a valid script is:

```lua
local light = access.Light.officeLight

assert(light.value.brightness >= 0)
assert(light.value.brightness <= 100)

light.propose({
    value = { brightness = 100 },
    priority = "high",
    duration_ms = 5
})

light.propose({
    value = { brightness = 30 },
    priority = "low",
    lifetime = "persistent"
})
```

Invalid examples:

```lua
-- Invalid: direct snapshot mutation does not request an intent.
access.Light.officeLight.value.brightness = 100

-- Invalid: colon syntax passes two arguments.
access.Light.officeLight:propose({ value = { brightness = 100 } })

-- Invalid: owner, slot, and reason are unknown request fields.
access.Light.officeLight.propose({
    owner = 7,
    slot = 2,
    reason = "make the room brighter",
    value = { brightness = 100 }
})

-- Invalid: a read-only capability has no propose function.
access.Light.hallLight.propose({ value = { brightness = 0 } })

-- Invalid: unavailable libraries and dynamic loading are not part of _ENV.
local os = require("os")
```

---

## 14. Solid v0.1 Contract

The owner-approved Solid v0.1 completion contract is normative in the focused documents under `docs/`:

- `PUBLIC_API.md` defines the installed framework surface, handles, codecs, and mutation boundary;
- `EVENT_FORMAT_V1.md` defines canonical records, durability, recovery, checkpoints, and retention;
- `THREADING.md` defines owner-thread confinement and the bounded concurrent feedback boundary;
- `ADAPTER_CONTRACT.md` defines effects, commands, reports, timing, retry, idempotency, and reconciliation;
- `REPLAY.md` distinguishes generic projection from host-assisted execution verification;
- `COMPATIBILITY.md`, `SECURITY_BOUNDARY.md`, and `SUPPORT.md` state the release claims and limits.

The v0.1 frame order is `begin -> snapshot/apply queued feedback -> expire -> systems -> expire same-frame intents -> resolve desires -> reconcile commands -> record issuance/attempts -> dispatch -> apply permitted synchronous immediate reports -> durably complete or fail -> end`.

Remaining open decisions belong to later Liquid stages: Go integration, LLM integration, adaptive behavior triggers and policy, real hardware protocols, voice, biosignals, and final Liquid Layer application behavior. They must not weaken the deterministic Solid authority boundary.

---

## 15. Source Notes

This document builds on the earlier NeurOS design baseline and the implementation discussion that followed it.

External references supporting the direction:

1. Flecs documentation describes ECS entities as identifiers and components as data attached to entities. This supports separating identity, data, and systems.
   - https://www.flecs.dev/flecs/md_docs_2EntitiesComponents.html
2. Bevy describes entities as unique things assigned groups of components, then processed by systems over component types. This supports component-type query signatures.
   - https://bevy.org/learn/quick-start/getting-started/ecs/
3. EnTT's registry/storage/query model supports component-type storage and queries without making commands or intents into components.
   - https://github.com/skypjack/entt/wiki/Entity-Component-System
4. Unity ECS documentation emphasizes data-oriented ECS for control and determinism, and its component model separates component data from systems.
   - https://unity.com/ecs
   - https://docs.unity.cn/Packages/com.unity.entities%401.0/manual/concepts-components.html
5. Home Assistant's AI agents article explicitly warns that AI models hallucinate and should not be completely trusted, while also showing that AI can be useful through constrained APIs such as intents.
   - https://www.home-assistant.io/blog/2024/06/07/ai-agents-for-the-smart-home/
6. Modern CMake project structure commonly separates public headers under `include/<project>/`, implementation under `src/`, apps, tests, docs, and external dependencies.
   - https://cliutils.gitlab.io/modern-cmake/chapters/basics/structure.html
7. Architecture Decision Records capture significant decisions, context, and consequences, and will be used later to avoid losing reasoning.
   - https://adr.github.io/
   - https://docs.arc42.org/tips/9-5/
