# Liquid L0 Implementation Specification

**Status:** Implementation companion to `docs/LIQUID_STAGE2_PLAN.md`  
**Milestone:** L0 — Model-Facing Lua Capability Contract  
**Date:** 28 August 2026

This document narrows L0 into implementation-level semantics. It does not add scope beyond the Stage 2 plan. If implementation evidence makes a C++ shape below awkward, preserve the semantics and choose the smallest cleaner API.

---

## 1. Describe the Lua boundary that already exists

Do not create a parallel capability system.

`LuaBehaviorRunner` already owns the facts that matter for model-facing Lua authoring:

- which component types are exposed to Lua;
- each script-visible type name;
- the `LuaComponentCodec<T>` used to encode readable snapshots and decode proposals;
- current component names and behavior permissions;
- the host-bound target behind each `propose` closure;
- the host-fixed behavior owner and monotonic time during execution.

The capability manifest should therefore be built **through the runner/binding boundary**, not by teaching `World` about models/prompts and not through a second independently maintained registry.

A likely additive direction is conceptually:

```cpp
LuaCapabilityManifest capability_manifest(
    World& world,
    BehaviorId behavior,
    IntentTime now
);
```

Exact naming/constness is an implementation decision. The invariant is that discovery reuses the same Lua bindings and current `World` permission source used by actual script execution.

Binding/model metadata must follow the existing runner freeze rule: it is configured before the runner begins execution and is not mutated underneath active scripts.

---

## 2. Read shape and write shape are separate

`LuaComponentCodec<T>` deliberately has two independent executable functions:

```text
encode(Component) -> LuaValue
decode(LuaValue) -> Component
```

Solid does not prove these functions are symmetric. A codec could legitimately expose a rich readable snapshot but accept a narrower write/proposal shape.

Therefore L0 must **not** assume one schema automatically describes both directions.

For every model-discoverable Lua binding, trusted metadata conceptually contains:

```text
readSchema
writeSchema
trusted bounded description
```

where:

- `readSchema` describes values produced by the Lua codec's `encode` side;
- `writeSchema` describes values accepted by the Lua codec's `decode` side.

Most simple components will use the same shape in both directions. Provide a convenient symmetric-registration/helper path if it keeps call sites readable, but represent the two semantics distinctly in the contract.

This is important for read-only capabilities too: a model needs a schema/description to interpret a sensor snapshot, not merely the raw current value.

---

## 3. `LuaValueSchema` describes exact `LuaValue` kinds

The schema vocabulary is **not JSON semantics**. It mirrors the representation transported by `LuaBehaviorRunner`.

| Schema kind | Exact accepted `LuaValue` storage |
| --- | --- |
| Boolean | `bool` |
| Integer | `std::int64_t` |
| Number | `double`, finite |
| String | `std::string` |
| Array | `LuaValue::Array` |
| Object | `LuaValue::Table` |

### No implicit numeric coercion in L0

`Integer` and `Number` remain distinct because the current `LuaValue` API/decoder distinguish them.

A `Number` schema does not silently accept an integer `LuaValue`. An `Integer` schema does not accept a floating `LuaValue` merely because the mathematical value is integral.

Authoring consequence: a future renderer must make clear that a writable `Number` expects a Lua floating literal such as `1.0`, while `1` is an Integer under the current Lua 5.4 boundary.

If a real codec later needs numeric coercion, specify it explicitly rather than quietly changing the L0 contract.

---

## 4. Schema constraints

### Boolean

Exact kind only. May carry a bounded trusted description.

### Integer

Optional inclusive `std::int64_t` minimum/maximum.

Reject schema configuration where minimum exceeds maximum.

### Number

Optional inclusive finite `double` minimum/maximum.

Reject NaN/infinite schema bounds and minimum greater than maximum.

### String

Finite maximum byte length, optional minimum byte length, optional finite enum.

Rules:

- minimum <= maximum;
- every enum entry obeys string limits;
- enum count and aggregate enum bytes are bounded;
- duplicate enum entries are rejected as invalid host metadata.

Use byte length consistently with the current Lua/string transport contract. Do not silently switch to Unicode code-point counts.

### Array

One child item schema and finite minimum/maximum item counts.

Rules:

- minimum <= maximum;
- maximum cannot exceed the configured L0/transport allowance;
- validation recurses through every item and counts aggregate structural nodes.

### Object

Finite named fields. Each field has:

- exact string key;
- child schema;
- required/optional flag;
- optional bounded trusted description if description is not already represented on the child node.

Unknown fields are rejected by default. Do not add an `additionalProperties=true` escape hatch without a real codec requirement.

Object keys are data keys, not assumed Lua identifiers. A model may need bracket syntax for unusual keys in generated Lua values.

---

## 5. Lua empty-array limitation must remain explicit

Current Lua transport distinguishes host-encoded arrays using a private marker. This preserves an empty `LuaValue::Array` when a copied host snapshot enters Lua.

A literal Lua table `{}` has no array/object marker and is interpreted by the host as an **empty object**.

Consequences:

- non-empty arrays can be constructed with normal Lua array literals;
- a readable host array snapshot can be reused/cleared while preserving the private array marker;
- a write-only capability cannot currently construct a brand-new empty array using `{}`.

L0 must not advertise a fiction that every schema-valid value is necessarily constructible from arbitrary Lua source under the current language boundary.

The stable authoring contract exposed beside a manifest must state the empty-array rule. If a write-only Array capability requires an empty value, a future model should be able to report the current authoring limitation instead of inventing syntax.

Do **not** add an array-construction helper in L0 unless a real current model-visible codec makes it necessary. If it becomes necessary, treat it as an explicit additive Lua API decision with focused sandbox/tests.

---

## 6. Trusted descriptions are useful but deliberately small

Shape alone is not sufficient for authoring. For example:

```text
brightness: Integer 0..100
```

is more useful when trusted metadata can also state that it represents brightness percentage/intensity.

L0 may support bounded descriptions on the binding and schema fields/nodes.

Rules:

- descriptions come from trusted host registration code;
- description bytes per node/binding and aggregate manifest bytes are bounded;
- descriptions never affect validation or permission;
- no ontology, planner, localization system, user-profile language, or free-form runtime description registry is introduced.

Dynamic user/sensor/device/integration strings are not trusted instructions. Future renderers carry those as structured/delimited data.

Per-instance application semantics are not required in L0. A component instance name remains the instance identity exposed by Solid; richer room/device/user meaning belongs to future application context unless a reusable Liquid requirement is demonstrated.

---

## 7. Manifest contract

A `LuaCapabilityManifest` is an immutable copied authoring view for one prepared behavior at one host capture time.

At minimum the semantic manifest contains:

```text
authoring contract/version
current monotonic now_ms
capabilities[]
```

Each capability contains conceptually:

```text
exact access expression
script-visible type name
component instance name
current model-visible access
trusted bounded binding description (if registered)
read schema + copied readable value, when readable
write schema, when writable
```

### Permission projection

For a fully model-described binding:

| Current `World` permission | Manifest read side | Manifest write side |
| --- | --- | --- |
| Read | `readSchema` + copied value | absent |
| Write | absent | `writeSchema` |
| ReadWrite | `readSchema` + copied value | `writeSchema` |

The manifest never infers permission from snapshot/schema presence.

The real Lua entry may expose only the sides currently granted by `World`, exactly as it does today.

### Missing model metadata

The simplest L0 rule is: a binding is model-discoverable only when the trusted registration supplies the model metadata required for both codec directions. Human/trusted schema-less execution remains supported.

A convenience helper may register the same schema for read/write when the codec is symmetric.

Do not try to infer a missing read or write schema from snapshots or from the other direction.

### Capture identity

The host needs enough private/internal capture identity to enable stale-authority checks later, e.g. world identity + behavior identity + behavior access revision or an equivalent opaque capture token.

Do not make raw world-local handles part of a future model-facing serialized contract merely because the C++ implementation uses them internally.

L0 does not implement proposal stale checking; it must avoid making it impossible later.

---

## 8. Authoring-contract version is independent

Do not conflate:

```text
LuaBehaviorScript.revision
ComponentSchema.version
Lua model-authoring contract version
```

- behavior revision identifies one script source revision;
- component schema version identifies the Solid codec/compatibility contract;
- authoring-contract version identifies the model-visible Lua language/capability rules (`access`, `propose`, lifetime fields, named intents, watches, array limitations, etc.).

Use a small stable marker owned by the Lua boundary, independent of model/provider version.

---

## 9. Exact access paths are generated by the host

The model must never guess whether a type or component name is safe dot syntax.

Examples:

```text
access.Light.officeLight
access["Lighting Device"]["office-light"]
access.Light["end"]
```

Recommended deterministic source-generation rule:

1. Use dot notation only for an ASCII Lua identifier matching `[A-Za-z_][A-Za-z0-9_]*` that is not a Lua keyword.
2. Otherwise use bracket indexing with a double-quoted Lua string literal.
3. Encode that string literal byte-for-byte:
   - printable ASCII other than `"` and `\` may be emitted directly;
   - `"` -> `\"`;
   - `\` -> `\\`;
   - all remaining bytes, including control/NUL/non-ASCII bytes, may be emitted as fixed-width three-digit decimal Lua escapes `\ddd`.
4. Never normalize or alter the underlying type/component identity string.

An ASCII-only escaped expression avoids depending on source-file Unicode normalization and handles arbitrary `std::string` keys deterministically.

Tests must prove generated expressions actually resolve the intended capability in the real Lua sandbox, not only compare expected text.

At minimum test:

- normal identifiers;
- Lua keywords;
- spaces/punctuation;
- quote/backslash;
- newline/control bytes;
- embedded NUL if current component naming permits it;
- UTF-8/non-ASCII byte sequences;
- mixed type/component cases where only one segment requires brackets.

---

## 10. Validation layering

L0 intentionally has multiple validators because they answer different questions.

```text
read LuaValueSchema
    "Does the encoded snapshot match what we told a model it may read?"

write LuaValueSchema
    "Does a candidate LuaValue match the documented proposal shape?"

LuaComponentCodec<T>::decode
    "Does trusted executable host code accept this exact proposed value?"

World permission
    "May this behavior currently read/write this component?"

Lua host closure + intent transaction
    "Is this operation still valid at actual execution/commit time?"
```

The schemas improve discoverability/early diagnostics. Executable codec/permission/transaction remain authority.

Do not claim arbitrary C++ `encode`/`decode` functions are formally equivalent to declarative schemas.

Pragmatic consistency evidence:

- known valid/invalid read/write fixtures;
- every readable snapshot emitted into a manifest validates against `readSchema`;
- write-schema fixtures are also passed through the real Lua codec decoder in tests;
- intentional schema/codec disagreement fails closed;
- schema metadata never creates permission that `World` denies.

---

## 11. Failure semantics

Exact C++ result/exception types may follow project style, but tests should distinguish these categories.

### Host configuration error

Examples:

- invalid range/count bounds;
- NaN/infinite Number bounds;
- duplicate object fields;
- duplicate enum values;
- schema/description exceeds construction limits;
- model-visible binding omits required read/write metadata.

Reject before the binding becomes model-discoverable.

### Manifest build error

Examples:

- behavior does not exist;
- readable snapshot unavailable;
- encoded readable value violates declared `readSchema`;
- aggregate manifest limits exceeded.

Fail closed. Never return a partial manifest that changes apparent authority.

### Normal absence

A binding registered only through the existing schema-less `expose_component(...)` API remains valid for human/trusted Lua execution and simply does not participate in model discovery.

---

## 12. Bounds

L0 must bound all recursive and text growth.

Prefer limits aligned with `LuaExecutionLimits` and `ValueLimits` where appropriate.

Required categories:

- schema depth;
- schema total nodes;
- object fields;
- enum count and aggregate bytes;
- description bytes per node/binding and aggregate;
- declared string maximum;
- declared array maximum;
- capabilities per manifest;
- copied `LuaValue` aggregate nodes/bytes;
- access-expression bytes;
- diagnostic bytes.

A schema should not describe values larger/deeper than the Lua transport can actually carry without an explicit reason.

---

## 13. Source compatibility and registration shape

Current v0.1 remains valid:

```cpp
runner.expose_component(type, scriptName, codec);
```

Model-facing metadata is additive.

A likely clean conceptual type is:

```text
LuaModelBindingMetadata
    readSchema
    writeSchema
    description
```

with a helper/factory for symmetric codecs.

Possible API forms include a schema-bearing overload or an adjacent explicit metadata registration call. Choose the form that keeps one executable binding as the source of truth and makes drift/invalid registration difficult.

Do **not** create an independent model capability registry containing a second copy of type/component bindings.

Metadata registration follows the same pre-execution freeze as Lua bindings.

---

## 14. Tests before substantive implementation

Codex should start L0 by drafting tests and minimal headers, not core `.cpp` behavior.

### Schema tests

Cover:

- every exact value kind;
- Integer versus Number non-coercion;
- finite Number bounds / NaN / infinity rejection;
- invalid min/max definitions;
- String byte length and enum rules;
- Array count/recursive validation;
- Object required/optional fields;
- unknown-field rejection;
- duplicate fields/enums;
- depth/node/description/enum-byte limits;
- deterministic bounded diagnostics.

### Codec-direction tests

Use at least:

1. a symmetric fixture such as `Light { brightness: Integer 0..100 }`;
2. an intentionally asymmetric test codec whose `encode` and `decode` shapes differ.

Prove that L0 does not accidentally substitute one schema for both directions.

For write fixtures, schema-valid candidate values should also be exercised against the actual Lua codec decoder. For read fixtures, actual encoded snapshots must validate against `readSchema`.

### Manifest tests

Cover:

- Read => `readSchema` + value only;
- Write => `writeSchema` only;
- ReadWrite => both sides;
- permission comes from current `World`, not metadata presence;
- access revocation/removal reflected after rebuild;
- schema-less binding absent from model discovery but still executable;
- copied values do not alias component memory;
- stable deterministic ordering;
- exact `now_ms` capture;
- exact authoring-contract version;
- trusted description bounds;
- access-path generator edge cases and real sandbox lookup;
- aggregate manifest limits;
- read-schema mismatch fails the complete manifest rather than emitting partial state.

### Empty-array authoring test/documentation

Prove the existing behavior:

- host-provided empty array snapshot retains Array identity;
- literal `{}` is read as Object;
- L0 metadata does not falsely claim a write-only empty array has a magic construction syntax.

No new helper is required unless implementation encounters a real model-visible codec that needs it.

### Compatibility tests

Cover:

- old schema-less `expose_component` source compiles unchanged;
- existing Lua behavior/lifecycle tests remain unchanged where possible;
- no new Core dependency on Lua/model metadata;
- Core-only build stays valid.

---

## 15. CMake/package expectations

L0 remains in `Liquid::Lua`.

Expected new files:

```text
include/liquid/scripting/LuaValueSchema.hpp
include/liquid/scripting/LuaCapabilityManifest.hpp
src/scripting/LuaValueSchema.cpp
src/scripting/LuaCapabilityManifest.cpp
tests/test_lua_schema.cpp
tests/test_lua_manifest.cpp
```

Add them to the existing `liquid_lua_component` and test configuration. Do not add a new exported target or dependency.

Installed `Liquid::Lua` consumers receive the new public scripting headers once L0 completes.

---

## 16. Explicit non-goals

L0 does not:

- invoke a model;
- render an OpenAI/Anthropic request;
- implement prompt repair;
- add Hermes;
- add MCP;
- create/approve/activate generated behavior;
- inspect arbitrary runtime intent resolution;
- add remote mutation;
- change intent lifetime semantics;
- change `Runtime::run_frame()`;
- add a behavior DSL/IR;
- solve application trigger/revalidation policy;
- solve the write-only empty-array limitation unless a current real codec makes it necessary.

---

## 17. L0 exit test

L0 is complete when trusted host code can prepare a normal Solid behavior, register the existing Lua bindings plus model-facing read/write metadata, and obtain a bounded immutable manifest that tells an external author exactly:

```text
what Lua paths exist
what each binding/field means
what may be read
what exact shape readable values have
what readable values exist now
what may be proposed
what exact shape proposals must have
what monotonic time and authoring contract apply
what Lua construction limitations apply
```

while the actual Lua codec, `World` permission, host closures, and transactional intent path retain all authority and every existing v0.1 schema-less Lua binding continues to work unchanged.
