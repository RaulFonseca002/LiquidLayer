# L0 — Exact Lua schemas and capability manifests

**Status:** specified; inactive. **Dependency:** reconciled Solid baseline.
Apply [the common contract](LIQUID_IMPLEMENTATION_CONTRACT.md).

## Outcome and existing evidence

Trusted host code registers model metadata beside an executable Lua binding
and obtains a copied manifest for one existing behavior. No generated source
is executed by discovery. Existing schema-less callers keep their behavior.

Read `include/liquid/scripting/LuaBehaviorRunner.hpp` (`LuaValue`, codec,
limits, `Binding`, `expose_component`) and `src/scripting/LuaBehaviorRunner.cpp`
(`register_binding`, value transport, capability preparation). Binding names
already reject NUL and duplicates; first execution freezes registration.
`World::behavior_access_revision` is authority metadata, not a value revision.

## Public API and ownership

Keep additions in `liquid::scripting` / `Liquid::Lua`:

```cpp
// Additional overload; the existing three-argument overload is unchanged.
template <typename T>
void LuaBehaviorRunner::expose_component(
    ComponentType<T> type, TypeName scriptName,
    LuaComponentCodec<T> codec, LuaModelBindingMetadata metadata);

LuaManifestResult LuaBehaviorRunner::capability_manifest(
    World& world, BehaviorId behavior, IntentTime now);

LuaSchemaValidation validate_lua_value(
    const LuaValueSchema& schema, const LuaValue& value);
```

`LuaValueSchema` is a value-like immutable tree, privately held through const
nodes. Public static factories are `boolean`, `integer`, `number`, `string`,
`array`, `object`; construction validates the complete tree. The public factory
inputs below are the API contract; no mutable node access or reference cycles.
Copies may share immutable nodes; validate aggregate limits per expanded tree.

| Factory | Inputs besides optional description |
| --- | --- |
| boolean | none |
| integer | optional int64 minimum and maximum |
| number | optional finite double minimum and maximum |
| string | minimumBytes, maximumBytes, optional vector of enum byte strings |
| array | item schema, minimumItems, maximumItems |
| object | vector of `LuaSchemaField{name, schema, required}` |

All factory arguments except description and explicitly optional bounds are
required. `LuaModelBindingMetadata` contains required `readSchema`, required
`writeSchema`, and optional description. `symmetric_metadata(schema, description)`
returns metadata with both directions. No adjacent metadata-only registration:
the four-argument overload validates everything before registering one binding.
Store metadata on that binding; never build a second binding registry.

`LuaSchemaValidation` has `valid`, code (`Kind`, `Range`, `MissingField`,
`UnknownField`, `Limit`), and bounded field path/diagnostic. On success no error
is present. Visit fields in unsigned-byte lexical order and arrays by index;
return the first failure deterministically. Invalid schema configuration throws
`invalid_argument`; mutation after freeze throws `logic_error`.

`LuaManifestResult` contains either one complete `LuaCapabilityManifest` or
`LuaManifestError{InvalidBehavior, SnapshotUnavailable, SchemaMismatch,
LimitExceeded, HostError}` plus bounded diagnostic. The runner catches codec
exceptions and does not return partial authority. `now` is host-supplied, must
fit the current Lua integer time range, and does not advance Runtime time.

The first successful manifest capture also freezes binding registration. This
is an explicit rule of the new discovery API; old callers still freeze on
first execution. Failed capture does not freeze a previously unfrozen runner.
Expose a read-only runner binding identity and effective execution limits in
the manifest's private capture data for later staleness checks.

## Exact schemas and limits

Kinds match `LuaValue::Storage`: Boolean=bool, Integer=int64, Number=finite
double, String=byte string, Array=vector, Object=string-keyed table. No null,
union, implicit numeric coercion, regex, references or arbitrary JSON Schema.
`1` and `1.0` remain distinct at the Lua boundary.

Bounds are inclusive; reject inverted ranges, nonfinite Number bounds,
duplicate object names and enum entries. An explicitly present empty enum
is invalid; absent enum means any string within bounds. Unknown object fields
are always rejected. Optional means absent, not null. Descriptions must be
valid UTF-8 without NUL and cannot alter validation. Object field keys are
arbitrary bytes, not Lua identifiers.

| L0 limit | Default / ceiling |
| --- | --- |
| Schema container nesting | 16; root container counts as 1 |
| Expanded nodes per schema | 4,096 |
| Fields per object / maximum array length | 256 / 4,096 |
| Declared maximum string bytes | 65,536 |
| Enum entries / aggregate enum bytes | 256 / 65,536 per schema |
| Description bytes per node/binding | 1,024 |
| All description bytes per manifest | 65,536 |
| Field/name bytes / generated access expression | 256 / 4,096 |
| Manifest capabilities / copied value nodes | 128 / 16,384 |
| Whole manifest logical bytes / diagnostic | 1 MiB / 4,096 |

The four-argument overload additionally checks schemas against the runner's
effective `maxTableDepth`, `maxTableEntries`, `maxStringBytes` and buffered-value
budget. Compute worst-case nested collection entries, keys and strings with
checked arithmetic; reject metadata whose declared maximum cannot fit one
transported value. Description/schema budgets are separate from Lua VM memory.
For conservative depth alignment require schema container count no greater
than `maxTableDepth`; do not rely on an off-by-one allowance in Lua recursion.

For opt-in described bindings, actual proposals validate `writeSchema` before
the executable decoder, and readable snapshots validate `readSchema` before
entering Lua or a manifest. Metadata/codec disagreement fails the bundle or
manifest. The old schema-less path does neither new schema check. The codec,
World permissions and transaction still decide executable authority. Tests
provide consistency evidence, not a proof of arbitrary C++ codec equivalence.

## Manifest and source construction

Public manifest fields: `authoringContract="liquid.lua.authoring/1"`, `nowMs`,
effective execution limits, construction notes, and ordered capabilities.
Each capability has script type name, component name, exact Lua access expression,
access mode, description, optional read schema/value and optional write schema.

| Current permission | Read side | Write side |
| --- | --- | --- |
| Read | copied encoded value + read schema | absent |
| Write | absent | write schema |
| ReadWrite | both read fields | write schema |

Schema-less bindings are omitted. A described binding missing either schema is
invalid configuration even for a currently read-only behavior. Sort entries by
script type and component name using unsigned-byte lexical order. Snapshot
values and metadata contain no component borrows, registries or raw handles.
Private capture data retains world/behavior identity, access revision and
binding identity; it is never serialized to an author.

Generate dot syntax only for ASCII non-keyword Lua identifiers. Otherwise use
bracketed double-quoted strings: escape quote/backslash, emit other printable
ASCII directly, and encode every remaining byte with a three-digit decimal Lua
escape. Do not normalize names. Script-visible binding names reject NUL under
the existing contract. World rejects empty component names but permits embedded
NUL in nonempty names; test a NUL-containing component name with a normal binding
name and prove that the generated expression addresses its full byte identity.

Always include these authoring notes: fresh VM; host-fixed owner/time; named
lifecycle proposals; persistent/until-time lifetime; no physical-success claim;
integer/float distinction; literal `{}` is Object. Host-encoded arrays retain a
private array marker, but Lua has no new write-only empty-array constructor.
Do not reject all Array schemas or invent a helper: document this limitation
and test actual source construction through the sandbox.

## Implementation steps and tests

| Step | Implement after its failing test | Required oracle |
| --- | --- | --- |
| L0.1 | Immutable schema factories and validator | All six kinds; exact numbers; invalid bounds/enums/fields; optional/unknown fields; depth/size at limit and limit+1; deterministic error path |
| L0.2 | Four-argument binding overload and opt-in enforcement | Symmetric brightness and asymmetric read/write fixtures; wrong schema/codec direction rejected; old overload still executes; failed registration leaves no binding |
| L0.3 | Behavior manifest and freeze-on-success | Read/Write/ReadWrite projections; absent metadata; failed snapshot/allocation; no partial manifest; copied values; deterministic time/order; successful freeze and failed-capture retry |
| L0.4 | Expressions, limits and installed surface | Real Lua lookup of keyword/punctuation/escape/non-ASCII names; binding NUL rejection; `{}` versus host empty Array; revoke/remove rebuild; aggregate limits; Core-only and installed Lua consumers |

Register CTest names `lua_schema` and `lua_manifest`. Focused command after
build: `ctest --test-dir build/strict -R '^lua_(schema|manifest)$' --output-on-failure`.
Also run existing scripting/lifecycle tests and all common gates. Do not claim
execution permission from merely matching an expression or validating source text.

## Allowed files and exit

New: `include/liquid/scripting/LuaValueSchema.hpp`,
`include/liquid/scripting/LuaCapabilityManifest.hpp`, corresponding
`src/scripting/LuaValueSchema.cpp` and `LuaCapabilityManifest.cpp`,
`tests/test_lua_schema.cpp`, `tests/test_lua_manifest.cpp`.
Existing: runner header/implementation, focused scripting/lifecycle tests,
CMake, Lua consumer example, and affected documentation.

No World/Runtime behavior change, new target, provider, authoring session,
proposal activation, or transport in L0. Exit requires the whole immutable
manifest contract and legacy compatibility, then independent review and owner
activation of L1.
