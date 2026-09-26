# L1 — Prospective scope and immutable proposals

**Status:** specified; inactive. **Dependency:** L0 accepted and landed.
Apply [the common contract](LIQUID_IMPLEMENTATION_CONTRACT.md).

## Outcome and design choice

A trusted host selects prospective grants without creating a live behavior.
An external caller receives a runner-backed manifest and submits exact Lua
source as an immutable proposal. Submission does not run Lua or mutate World.

Use a separate host-owned scope object, not a prepared live behavior. Creating
even an empty behavior can trigger system membership callbacks; omitting a
script is not sufficient isolation. Reuse the runner's executable binding
catalog instead of introducing an independent component/codec registry.

## Public interfaces

Add `liquid::authoring::AuthoringSession` and common result/ID types. Its
constructor receives `AuthoringSessionId`, `Runtime&`, `LuaBehaviorRunner&`,
registered `ComponentType<LuaBehaviorScript>`, and finite `AuthoringLimits`
(common defaults). Validate that the script type belongs to this World.

| Method | Caller and input | Result |
| --- | --- | --- |
| `create_scope` | trusted host: scope owner, vector of `ScopeGrant`, monotonic now | ScopeId + manifest |
| `replace_scope` | trusted host: ScopeId, expected revision, complete replacement grants, now | new revision + manifest |
| `revoke_scope` | trusted host: ScopeId, expected revision | revoked state |
| `discover` | CallerContext, ScopeId, now | copied current manifest + revision |
| `submit` | CallerContext, ScopeId, expected revision, source, rationale, optional expected managed revision | immutable ProposalId |
| `proposal` | CallerContext, ProposalId | immutable public record |

L1 defines `scripting::LuaScopeGrant` in the existing Lua manifest header;
`authoring::ScopeGrant` is an alias, not a second representation. The runner
accepts `span<const LuaScopeGrant>` and never includes an Authoring header.
This keeps the Lua → Authoring dependency direction impossible. The grant
contains script-visible type name, component name, and Read/Write/ReadWrite
mode. Only trusted host APIs accept grant lists. The host chooses the
caller and policy; neither a source string nor a manifest can grant access.
Duplicate targets, missing names, absent metadata and unknown types fail the
whole scope. At least one grant is required. Script-control components are
never exposed by an authoring scope, even if a host registered their codec.
Compare the explicitly supplied script type identity, not a guessed schema name.

L1 adds a host-only runner method
`scope_manifest(World&, std::span<const LuaScopeGrant>, IntentTime)`
using the same `Binding` metadata and exact expression builder as L0. Add typed
binding callbacks to resolve target identity and encode a host-selected readable
component without a BehaviorId. World already exposes typed component lookup;
this is trusted host authority, not a new model-selected owner. Keep callbacks
private and freeze this path on successful capture just like L0.

The manifest's private capture discriminates existing-behavior captures from
prospective-scope captures; the latter have no live BehaviorId. The session
stores private current generational target handles and immutable
runner identity with each scope revision. A scope references live data; it
does not own component values or attach a behavior to the live Runtime.
No read is performed for a Write-only grant. Returned copies follow L0 limits.

## Proposal records and validation

`BehaviorProposal` contains ProposalId, session/scope IDs, scope revision,
contract version, exact source, bounded rationale, and optional pair
`ManagedBehaviorId + expectedRevision`. Private capture data retains target
identities and binding identity. Rationale is untrusted data, never instructions.
Proposal records cannot be edited; a repair is a new proposal.

Submission validates encoding, source bounds, record capacity, scope ownership,
scope revision, and target/binding liveness. It does not claim Lua syntax,
referenced paths, branches or dynamic proposal values are valid. No regex or
AST scan substitutes for execution. L2 supplies actual syntax/sandbox/codec
evaluation. A proposal without the managed pair means a new behavior.

A supplied managed pair must resolve to an active record belonging to this
scope and match its current revision. Until L5 creates such records, a full
pair returns NotFound; a malformed pair still returns InvalidInput. Merely
reserving the ID type does not implement or authorize managed behavior.

All fields other than rationale and the complete managed pair are required;
half a managed pair fails. Source must be nonempty UTF-8 without NUL and at
most the smaller of the session and runner source limits. No trimming, newline
normalization or source re-rendering. Exact bytes are used in every later step.

## Staleness and errors

Re-resolve target handles for every capture/submit. Same names with new slot
generations are stale, not equivalent. `replace_scope` increments revision
even if grants compare equal; it invalidates old proposals for further actions.
Revocation prevents discovery and later evaluation/approval. Already active
behaviors are not automatically stopped by authoring-scope revocation; L5's
host operation handles that separately. State values may change without scope
revision: discover returns new copies and capture time, never old cached values.

On failure create no scope/proposal and change no World topology or intents.
Monotonic IDs may have gaps after failed admission but are never reused.
Session capacity exhaustion rejects new records without evicting old ones.
No cryptographic hash or secret ID is needed for in-process exact artifact
lookup; enforce CallerContext checks even when an ID is known.

## Implementation steps and tests

| Step | Implement after its failing test | Required oracle |
| --- | --- | --- |
| L1.1 | Opt-in package target and common IDs/results | Old Core/Lua/Simulation consumers unchanged; enabled Authoring consumer works; disabled/missing dependency/unknown component fails correctly |
| L1.2 | Scope capture through runner callbacks | Zero live behavior/system/intent changes; Read/Write projection; two callers cannot see each other's scopes; no schema-less/script-control grant |
| L1.3 | Immutable proposal admission | Exact source round trip; bad UTF-8/NUL/size; duplicate/unknown fields at boundary; no source execution; repair creates distinct record |
| L1.4 | Revision, revoke, removal/recreation and capacity | ABA target rejection, stale expected revision, copied current values, no silent eviction, counter exhaustion and cross-session rejection |

Register `authoring_scope`, `authoring_proposal`, `authoring_package` CTests.
Run `ctest --test-dir build/strict -R '^authoring_(scope|proposal|package)$' --output-on-failure`
and the common full gates with Authoring enabled.

## Allowed files and exit

New: `include/liquid/authoring/Types.hpp`, `AuthoringSession.hpp`,
`src/authoring/AuthoringSession.cpp`, `tests/test_authoring_scope.cpp`,
`tests/test_authoring_proposal.cpp`, `examples/installed-package/authoring.cpp`.
Existing: L0 manifest/runner files and tests; package wiring and consumer tests
listed in the common contract; affected docs. Add no model/network dependency.

Exit: bounded prospective authoring works without live topology side effects,
every proposal has an exact immutable identity, and the opt-in package is proven.
No simulation, approval, activation or generic runtime inspection is claimed.
