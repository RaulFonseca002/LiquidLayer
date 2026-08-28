# Liquid L0 Implementation Specification

**Status:** Implementation companion to `docs/LIQUID_STAGE2_PLAN.md`  
**Milestone:** L0 — Model-Facing Lua Capability Contract  
**Date:** 28 August 2026

This document narrows the approved L0 milestone into implementation-level semantics. It does not add scope beyond the Stage 2 plan. If implementation evidence makes a shape below awkward, preserve the semantics and adjust the smallest public API needed.

---

## 1. L0 exists to describe the Lua boundary that already exists

Do not create a parallel capability system.

The existing `LuaBehaviorRunner` already owns the facts that matter for model-facing Lua authoring:

- which component types have been exposed to Lua;
- the script-visible type name;
- the `LuaComponentCodec<T>` used for copied snapshots and proposals;
- the real behavior's current component names and permissions;
- the exact host-bound target behind each `propose` closure;
- the current host-fixed `BehaviorId` and `IntentTime` during execution.

Therefore the model-facing manifest should be built **through the Lua runner/binding boundary**, not by teaching `World` about model prompts or by creating a second registry of capabilities.

A likely public direction is an additive `LuaBehaviorRunner` operation conceptually equivalent to:

```cpp
LuaCapabilityManifest capability_manifest(
    World& world,
    BehaviorId behavior,
    IntentTime now
);
```

The exact name/constness may change during header/test design. The important rule is that manifest construction reuses the same registered Lua bindings and current `World` permissions as execution.

---

## 2. `LuaValueSchema` describes exact `LuaValue` kinds

The schema vocabulary is **not JSON semantics**. It mirrors the value representation currently transported by `LuaBehaviorRunner`.

| Schema kind | Exact accepted `LuaValue` storage |
| --- | --- |
| Boolean | `bool` |
| Integer | `std::int64_t` |
| Number | `double`, finite |
| String | `std::string` |
| Array | `LuaValue::Array` |
| Object | `LuaValue::Table` |

### No implicit numeric coercion in L0

`Integer` and `Number` remain distinct because the existing `LuaValue` API and Lua decoder distinguish them.

A schema `Number` must not silently accept an integer `LuaValue`, and an `Integer` schema must not accept a floating-point `LuaValue` merely because it has an integral mathematical value.

This matches the actual codec boundary and prevents the model-facing schema from promising conversions the host does not perform.

Authoring consequence: when a writable schema requires `Number`, future model instructions should make clear that a Lua floating literal such as `1.0` is different from integer literal `1` under the current boundary.

If a future real codec demonstrates that numeric coercion is desirable, add it as an explicit codec/schema contract rather than silently changing L0 semantics.

---

## 3. Schema constraints

### Boolean

No value constraints beyond exact kind. May have a bounded trusted description.

### Integer

Optional inclusive `std::int64_t` minimum/maximum.

Construction/configuration rejects `minimum > maximum`.

### Number

Optional inclusive finite `double` minimum/maximum.

Schema bounds themselves must be finite. Construction/configuration rejects NaN, infinities, and `minimum > maximum`.

### String

Required finite maximum length. Optional minimum length and finite enum.

Rules:

- minimum cannot exceed maximum;
- enum entries must individually satisfy length limits;
- enum list count and aggregate bytes are bounded;
- duplicate enum entries should be rejected as host configuration noise rather than preserved.

Use byte lengths consistently with the current Lua/string transport contract unless implementation proves a Unicode code-point semantic is required.

### Array

One item schema plus finite minimum/maximum item count.

Rules:

- minimum cannot exceed maximum;
- maximum must be explicit or bounded by the global L0 schema limits;
- validation is recursive and counts aggregate structural nodes.

### Object

A finite set of named fields. Each field has:

- exact field name;
- child schema;
- required/optional flag;
- optional bounded trusted description if not already carried on the child schema.

Unknown fields are rejected by default in L0. Do not add an `additionalProperties=true` escape hatch without a real codec requirement.

Field names are data keys, not Lua identifier assumptions. Future Lua source may need bracket syntax for fields that are not valid identifiers.

---

## 4. Lua empty-array limitation must stay visible

The current Lua boundary intentionally distinguishes host-encoded arrays with a private marker. A host-provided empty `LuaValue::Array` therefore remains an array when transported into Lua.

A literal Lua table `{}` has no array/object information and is interpreted by the host as an **empty object**.

Consequences for model authoring:

- non-empty arrays can be written with normal Lua array literals;
- an existing readable array snapshot can be reused/cleared while preserving the host array marker;
- a write-only capability cannot currently construct a brand-new empty array literal through `{}`.

L0 must not hide this mismatch.

The stable authoring-contract metadata/documentation exposed with a manifest must state this rule. A model faced with a write-only Array capability and a requirement to produce an empty array should be able to report that the current authoring boundary cannot express that exact value instead of hallucinating a syntax.

Do **not** add an array-construction helper in L0 unless a real current model-visible codec requires it. If that requirement appears, treat the helper as a deliberate additive Lua API decision with its own tests rather than smuggling it into schema implementation.

---

## 5. Trusted semantic descriptions stay small

Pure shape is not enough for a model: `{ brightness: Integer 0..100 }` is more useful when the host can also say what `brightness` means and, where relevant, its unit.

L0 may therefore support short trusted descriptions at schema nodes and/or binding level.

Rules:

- descriptions are supplied by trusted host registration code;
- description bytes per node/capability are bounded;
- aggregate description bytes per manifest are bounded;
- descriptions never change validation or permission semantics;
- no ontology, natural-language planner, localization system, or arbitrary user-generated descriptions are introduced in L0.

Dynamic sensor/user/device strings are **not** trusted descriptions. Future renderers treat them as structured data.

---

## 6. Manifest contract

A `LuaCapabilityManifest` is an immutable copied authoring view for one prepared behavior at one host capture time.

Model-visible information should be sufficient to author against the existing Lua API without leaking authority-bearing internals.

At minimum the semantic manifest contains:

```text
authoring contract/version
current monotonic now_ms
capabilities[]
```

Each capability contains conceptually:

```text
exact access expression
type/script name
component instance name
permission
trusted bounded description metadata (if registered)
readable copied value (only when readable)
writable LuaValueSchema (only when writable)
```

### Permission projection

| Permission | copied value | writable schema |
| --- | ---: | ---: |
| Read | yes | no |
| Write | no | yes |
| ReadWrite | yes | yes |

The manifest does not infer permission from data presence.

### Capture identity

The trusted host needs enough identity internally to detect stale authoring context in later milestones, e.g. world/behavior/access revision or an equivalent opaque capture token.

Do not make raw world-local handles part of the model-facing serialized contract merely because implementation uses them internally.

L0 does not need to implement proposal stale-checking yet; it must avoid designing the manifest so that stale checking becomes impossible later.

---

## 7. Authoring-contract version is separate from behavior revision

Do not conflate:

```text
LuaBehaviorScript.revision
ComponentSchema.version
Lua model-authoring contract version
```

They answer different questions.

- `LuaBehaviorScript.revision` identifies a behavior source revision.
- `ComponentSchema.version` identifies a registered Solid component codec/schema contract.
- the L0 authoring-contract version identifies the model-visible Lua syntax/capability rules that explain how to use `access`, `propose`, lifetimes, named intents, watches, arrays, and other host facilities.

Use a small stable integer/string marker owned by the Lua boundary. Do not version the model/provider.

---

## 8. Exact access paths are host generated

The model must never need to guess whether a type/component name is valid dot syntax.

Examples:

```text
access.Light.officeLight
access["Lighting Device"]["office-light"]
```

Path generation is deterministic host code and must correctly escape at least:

- `"`;
- `\`;
- control characters;
- spaces/punctuation;
- names beginning with digits;
- Lua keywords;
- UTF-8 names according to the chosen escaping rule.

Do not normalize or rewrite the underlying component name. The path is a source expression; type/component names remain their original identity strings.

Tests should prove the generated expression actually addresses the intended capability in the real Lua sandbox, not only compare escaped strings visually.

---

## 9. Validation layering

L0 deliberately has redundant validation layers because each one answers a different question.

```text
LuaValueSchema
    "Does this copied/candidate LuaValue match the model-visible shape?"

LuaComponentCodec<T>::decode
    "Does trusted host code accept this exact component value?"

World permission
    "May this behavior currently propose to this component?"

Lua host closure + transaction
    "Does the actual execution/commit remain valid right now?"
```

The first layer improves authoring and early diagnostics. The lower layers remain authority.

Do not attempt to prove arbitrary executable C++ codec functions are formally equivalent to declarative schema metadata.

Required consistency evidence is pragmatic:

- known valid/invalid fixture values;
- every readable encoded snapshot placed into a manifest passes its registered schema;
- intentional mismatch fixtures fail closed with bounded host diagnostics;
- schema metadata never causes a write permission to appear when `World` denies it.

---

## 10. Failure semantics

Exact exception/result types may follow existing project style, but tests should establish these categories distinctly:

### Host configuration error

Examples:

- invalid schema bounds;
- duplicate object fields;
- duplicate enum values;
- schema/description exceeds construction limits.

Reject before the binding becomes model-discoverable.

### Manifest build error

Examples:

- behavior does not exist;
- readable snapshot cannot be obtained;
- encoded readable value violates declared schema;
- aggregate manifest limits exceeded.

Fail closed. Do not emit a partial manifest that accidentally changes apparent authority.

### Normal absence

A Lua binding registered without model-facing metadata is still valid for existing human/trusted Lua execution but is absent from model discovery.

This is not a Runtime failure.

---

## 11. Bounds

Concrete defaults can be tuned against tests, but L0 must bound all recursive/text growth.

Prefer limits aligned with existing `LuaExecutionLimits`/`ValueLimits` rather than unrelated large values.

Required bound categories:

- schema depth;
- schema total nodes;
- fields per object;
- enum item count;
- enum aggregate bytes;
- description bytes per node/capability;
- description aggregate bytes;
- declared string maximum;
- declared array maximum;
- capabilities per manifest;
- copied `LuaValue` aggregate nodes/bytes;
- access-expression bytes;
- diagnostic bytes.

A schema should not be able to declare an authoring value larger than the Lua transport/execution boundary can actually carry without an explicit reason.

---

## 12. Source-compatibility rule

The current v0.1 API:

```cpp
runner.expose_component(type, scriptName, codec);
```

continues to compile and behave as it does today.

Model-facing metadata is additive. Reasonable implementation shapes include:

```text
schema-bearing overload
or
adjacent explicit model-metadata registration
```

Choose whichever keeps the execution binding single-sourced and makes invalid registration hard to express.

Do not duplicate the same component binding in two independent registries that can drift.

---

## 13. Tests before implementation

Codex should begin L0 by drafting tests and minimal headers, not `.cpp` behavior.

### Schema tests

Cover:

- each exact value kind;
- Integer versus Number non-coercion;
- finite Number bounds / NaN / infinity rejection;
- invalid min/max definitions;
- String length and enum rules;
- Array count/recursive validation;
- Object required/optional fields;
- unknown-field rejection;
- duplicate fields/enums;
- depth/node/description/enum byte limits;
- deterministic bounded diagnostics.

### Real codec consistency fixture

Use a representative codec such as:

```text
Light
  brightness: Integer 0..100
```

Prove:

- encoded valid snapshots satisfy schema;
- out-of-range/wrong-kind fixtures fail schema;
- codec remains the final validator.

### Manifest tests

Cover:

- Read / Write / ReadWrite exact projection;
- no permission inference from snapshot presence;
- access revocation/removal reflected after rebuild;
- only schema-bearing bindings are model-discoverable;
- copied values do not alias component memory;
- stable deterministic ordering;
- current `now_ms` captured exactly;
- authoring-contract version captured exactly;
- trusted description bounds;
- unusual access names and real sandbox lookup;
- aggregate manifest bounds;
- schema mismatch causes whole manifest failure rather than partial emission.

### Compatibility tests

Cover:

- existing schema-less `expose_component` source compiles unchanged;
- existing Lua behavior/lifecycle tests remain unchanged where possible;
- no new Core dependency on Lua/model-facing metadata;
- Core-only build remains valid.

---

## 14. CMake/package expectations

L0 remains part of `Liquid::Lua`.

Expected new files:

```text
include/liquid/scripting/LuaValueSchema.hpp
include/liquid/scripting/LuaCapabilityManifest.hpp
src/scripting/LuaValueSchema.cpp
src/scripting/LuaCapabilityManifest.cpp
tests/test_lua_schema.cpp
tests/test_lua_manifest.cpp
```

Add them to existing `liquid_lua_component` / test configuration. Do not add a new exported library target or dependency.

Installed `Liquid::Lua` consumers should receive the new public scripting headers once L0 is complete.

---

## 15. Explicit non-goals

L0 does not:

- invoke a model;
- render an OpenAI/Anthropic request;
- implement a prompt-repair loop;
- add Hermes;
- add MCP;
- create/approve/activate generated behavior;
- inspect arbitrary runtime intent resolution;
- add remote mutation;
- change intent lifetime semantics;
- change `Runtime::run_frame()`;
- add a behavior DSL/IR;
- solve application-level trigger/revalidation policy;
- solve the write-only empty-array limitation unless a real current codec makes that necessary.

---

## 16. L0 exit test

L0 is complete when trusted host code can prepare a normal Solid behavior, register the existing Lua bindings plus model-facing metadata, and obtain a bounded immutable manifest that tells an external author exactly:

```text
what Lua paths exist
what each capability means
what may be read
what may be proposed
what exact LuaValue shape is valid
what readable values exist now
what monotonic time and authoring contract apply
```

while the real Lua/World/codec/transaction boundaries retain all execution authority and every existing v0.1 script path still works unchanged.
