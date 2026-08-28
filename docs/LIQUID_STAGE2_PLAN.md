# Liquid Stage 2 Plan

**Status:** Owner-approved Stage 2 architecture and implementation plan  
**Date:** 28 August 2026  
**Foundation:** Solid v0.1.0 (`main`)  
**Current implementation milestone:** L0 — Model-Facing Lua Capability Contract

---

## 1. Purpose

This document closes the Stage 2 research phase and defines the implementation direction for **Liquid**.

Liquid is not an LLM provider, an agent runtime, a conversation system, or a smart-home application. It is the model-facing control and authoring layer over Solid: the reusable surface that lets an external model or agent understand, author, inspect, validate, and eventually operate Solid behaviors without bypassing Solid's deterministic authority.

The final **Liquid Layer** application decides which intelligence to use, when to invoke it, what user/environment context to provide, and what application policy gives that context meaning. A local model, a hosted model call, Hermes Agent, or a future agent runtime may all consume the same Liquid surface.

The central architecture is:

```text
                         LIQUID LAYER
      application policy / sensors / user context / AI routing
        ┌───────────────┼────────────────┐
        │               │                │
 direct local       hosted model       agent runtime
 inference             call             (e.g. Hermes)
        │               │                │
        │               │               MCP
        └───────────────┴─────────┬──────┘
                                  ▼
                               LIQUID
                  discover / author / observe
                  validate / evaluate / operate
                     model-friendly evidence
                                  │
                                  ▼
                                SOLID
              deterministic execution and authority
```

The Stage 2 implementation should grow only as real Liquid Layer scenarios require capabilities that the existing Solid contracts cannot already provide.

---

## 2. Architectural thesis

### 2.1 Solid owns execution truth

Solid remains the sole authority for:

- behavior identity and component access;
- immutable intent ownership, lifetime, and conflict resolution;
- deterministic frame execution;
- selected desires;
- effect command creation and dispatch;
- report correlation and validation;
- authoritative observed external state;
- durable runtime evidence and replay.

Liquid must preserve the distinction:

```text
live intent / desire
    ≠ selected intent
    ≠ dispatched command
    ≠ adapter report
    ≠ authoritative observed state
```

An intent that loses resolution remains alive until its lifetime expires or it is explicitly removed. Liquid must expose this fact accurately rather than treating "not selected" as "dead".

### 2.2 Lua remains the generated executable behavior boundary

Solid's Lua lifecycle boundary was deliberately designed for generated behavior:

- every behavior executes with a host-fixed owner;
- only explicitly exposed, typed, named capabilities are available;
- readable values are copied snapshots;
- writable capabilities create immutable intents through host-bound closures;
- named proposal/cancellation bundles commit transactionally;
- scripts receive no `World`, registries, raw slots, pointers, owner selection, OS/I/O/package/debug authority, or arbitrary native execution;
- execution is bounded by source, memory, instruction, value, table, string, diagnostic, intent, cancellation, and watch limits.

Therefore Stage 2 does **not** introduce a second generated behavior language before evidence requires one. The first authoring path remains an approved `LuaBehaviorScript` revision using the existing lifecycle contract.

Changing environment context does not silently rewrite an approved script. A source change is an explicit behavior revision and will eventually pass through proposal, validation/evaluation, and approval again.

### 2.3 Liquid owns a semantic API, not an AI runtime

Liquid should provide model-friendly capabilities such as:

- discover what a behavior may read and write;
- describe exact script-visible value shapes;
- return bounded copied snapshots;
- validate generated lifecycle Lua against the real host contract;
- later evaluate proposed behaviors in the real simulator;
- later inspect live behaviors, their intents, current selections, and observed state;
- later expose bounded operations that preserve Solid ownership and authority;
- later provide structured causal evidence suitable for an agent to explain.

Liquid should **not** decide:

- which model/provider to use;
- local versus hosted inference;
- direct model call versus agent runtime;
- when a model should be invoked;
- what "focus", "sensory overload", "task initiation", or other application concepts mean;
- which sensor/event should wake an agent;
- how conversational or long-term user memory is implemented.

Those are Liquid Layer responsibilities.

### 2.4 MCP is a transport adapter over Liquid

MCP is not the domain model of Liquid. The intended relationship is:

```text
Liquid semantic API
      │
      ├── direct in-process/application use
      │
      └── MCP adapter
              │
              ├── Hermes Agent
              ├── another MCP client
              └── future agent runtimes
```

This keeps Liquid independent from MCP protocol revisions and lets the same model-facing contract be used without an agent runtime.

As of MCP specification `2026-07-28`, the protocol core is stateless and Sampling is deprecated in favor of direct provider integration. Liquid's future MCP server therefore should expose tools and data; it should not use MCP Sampling as an internal model provider abstraction.

---

## 3. Direct inference and agent runtimes are separate axes

The architecture must not conflate deployment location with reasoning style.

These are all valid Liquid Layer choices:

```text
Direct + local
Direct + hosted
Agent + local
Agent + hosted
```

A direct inference is appropriate for bounded work such as classification or a structured interpretation. An agent runtime is appropriate when the task requires multiple tool calls, iterative correction, memory, conversation, or longer-running orchestration.

Hermes itself reflects this distinction: it has a full agent loop while also exposing direct structured LLM calls to plugins. Hermes can also route to custom OpenAI-compatible endpoints. It is therefore a useful Liquid consumer, but it is not equivalent to an inference provider and must not become a Liquid dependency.

A model may be stateless between calls. The system must not be.

When a model or agent is called again, current truth should be reconstructed from Liquid/Solid rather than trusted from conversational memory. Agent memory can preserve meaning and user history; Solid remains the authority for current runtime facts.

---

## 4. What Liquid Layer must eventually be able to do through Liquid

The following capabilities define the target surface. They are requirements for the Stage 2 roadmap, not permission to implement them all in L0.

### 4.1 Discover

An external model/agent must eventually be able to learn, within an explicitly scoped context:

- which component capabilities are available;
- their exact script-visible paths;
- whether each path is read, write, or read/write;
- the value shape accepted by each writable capability;
- current copied values for readable capabilities;
- relevant limits and lifecycle authoring rules.

A snapshot is never authority. Permission comes from trusted host state.

### 4.2 Author

An external model/agent must eventually be able to propose a lifecycle Lua behavior without receiving direct world mutation authority.

The host must be able to:

- validate the proposal structurally;
- validate claimed capability use;
- run it through the real Lua sandbox;
- evaluate it in simulation before activation;
- preserve the exact candidate source/revision being reviewed.

### 4.3 Observe

An external model/agent must eventually be able to reconstruct current runtime truth without reading internal registries or raw event files.

A model-friendly view should be able to answer, when relevant:

- what behaviors exist;
- which live intents a behavior owns;
- what each intent requests, its priority, lifetime, target, and stable name;
- whether a live intent is currently selected;
- which competing intent is selected for the target;
- which behavior owns that selected intent;
- the current authoritative observed state for external components;
- relevant command/report status;
- relevant recent causal evidence.

Liquid must compose this view from Solid's existing sources of truth rather than maintain a competing shadow runtime.

### 4.4 Operate

A future agent may need bounded operational actions that do not require rewriting an approved behavior, for example canceling an authorized owned intent, removing a behavior, or installing an approved revision.

These operations must be designed from ownership and capability rules. Liquid must never expose generic primitives equivalent to:

```text
write_any_component
mutate_world
execute_arbitrary_native_code
destroy_any_intent
raw_registry_access
```

The exact operational surface remains deferred until the read-only view and proposal lifecycle demonstrate what is actually required.

### 4.5 React

Liquid Layer needs to build reactive applications, but Liquid does not decide what changes are semantically important.

Liquid should eventually make bounded current state and change/evidence information available so an application can choose, for example:

```text
presence changed        -> maybe invoke a model
intent lost resolution  -> maybe ignore
repeated effect failure -> maybe wake an agent
```

The policy deciding whether to invoke a model belongs to Liquid Layer.

Do not introduce `SemanticTrigger`, `BehaviorCondition`, or similar core abstractions until multiple real scenarios demonstrate a common engine-level requirement that cannot be expressed with existing Lua, component watches, intent lifetimes, and application orchestration.

### 4.6 Explain

Liquid should eventually expose structured causal facts, not manufacture natural-language explanations inside the engine.

For example:

```text
observed office light = OFF
selected desire       = OFF from FollowUser
FocusSupport wants    = ON, persistent, still alive, not selected
OFF command           = applied and confirmed
```

The caller/model may turn those facts into language.

---

## 5. Architectural acceptance scenarios

These scenarios are not neurodivergence-specific engine concepts. They are tests that the abstraction remains generic while still supporting Liquid Layer.

### 5.1 Focus support with deterministic continuation

A prepared `FocusSupport` Lua behavior owns a persistent light intent while focus support is active.

If the user temporarily leaves, the behavior may use existing deterministic mechanisms such as an until-time intent or named intent replacement to encode a grace period. A model does not need to supervise every frame or every minute once the semantic decision has been converted into deterministic policy.

If an approved behavior source needs to change, that is an explicit revision rather than an implicit runtime adaptation.

### 5.2 Follow-user behavior conflicts with focus support

A second behavior follows the user through the house and proposes `OFF` for a light the user has left behind.

If the follow-user intent wins, the persistent FocusSupport `ON` intent remains alive. When the winning competing intent later disappears, the original intent can become selected again without being reconstructed and without invoking a model.

A model called later should be able to discover all three facts independently:

```text
FocusSupport intent ON is alive
FocusSupport intent ON is not currently selected
authoritative observed light state is OFF
```

This scenario is a required acceptance case for the later runtime-view milestone.

### 5.3 Stateless agent reconstruction

An agent process may restart or a different model may be selected by Liquid Layer. The new caller should be able to reconstruct the current relevant state through Liquid without relying on a private conversation transcript for runtime truth.

### 5.4 Two AI strategies, one Liquid surface

The same future Liquid API should be usable by:

- a direct local structured model call chosen by Liquid Layer; and
- an agent runtime such as Hermes using the MCP adapter.

The engine should not need different authority rules for those callers.

---

## 6. Frozen Solid constraints for every Liquid milestone

Every Stage 2 design must satisfy all of the following.

1. `Runtime` remains the sole frame-phase driver.
2. `World` remains the public state/behavior/component boundary.
3. Registries, `Coordinator`, `WorldState`, raw slots, borrowed pointers, and mutable internal state are not exposed to models or remote transports.
4. Generated executable behavior continues through the existing Lua sandbox unless a later milestone explicitly proves a replacement is needed.
5. Intent semantic contents remain immutable after creation.
6. A losing intent remains live until normal lifetime/cleanup/cancellation removes it.
7. Selected desire remains distinct from command, report, and observed physical truth.
8. External state is not optimistically mutated from selection.
9. Model calls do not block `Runtime::run_frame()`.
10. Solid's event format v1 is not expanded casually with model prompts, conversational state, or proposal metadata.
11. `World` and `Runtime` owner-thread confinement is preserved.
12. Existing v0.1 source compatibility is not weakened merely to make model integration easier.

---

## 7. Threading and transport rule

Solid's public contract confines `World` and `Runtime` to their creating thread. Future HTTP/MCP implementations are naturally concurrent or event-driven, so the transport must not retain a `Runtime&` and call it from arbitrary request threads.

The design rule is:

> Model-facing data is produced through the trusted owner-thread boundary as immutable copied views. Mutating requests are marshalled back to the owner-controlled execution path. A transport owns no Runtime authority of its own.

L0 only builds immutable capability data on the owner thread. The exact command queue, snapshot publisher, or host integration mechanism is deferred until an asynchronous transport milestone actually needs it.

---

## 8. Schema strategy

### 8.1 Why a schema is required

`LuaComponentCodec<T>` currently contains executable C++ `encode` and `decode` functions. A model cannot introspect those functions to learn that a field is required, an integer must be in a range, or an enum is limited to particular values.

A copied value also cannot safely communicate authority: seeing a value never implies permission to write it.

Therefore a trusted machine-readable description must be registered beside every **model-visible Lua codec**.

### 8.2 Do not reuse `ComponentSchema`

Solid already has `ComponentSchema`, which identifies a component's stable schema name/version for codec and compatibility contracts. Stage 2 must not overload that type with model-facing value-shape semantics.

The provisional L0 name is **`LuaValueSchema`** because the schema describes the value vocabulary actually transported by `LuaBehaviorRunner`.

### 8.3 V1 schema vocabulary

The canonical L0 vocabulary should intentionally be smaller than full JSON Schema and should match current `LuaValue` exactly:

- Boolean;
- signed Integer, optionally bounded;
- finite Number, optionally bounded;
- String with explicit size bounds and optionally a finite enum;
- Array with one item schema and explicit item-count bounds;
- Object with named fields and required/optional status, rejecting unknown fields by default.

Do not add in L0 unless a real codec requires them:

- null;
- bytes;
- unsigned-integer-only semantics;
- unions / `oneOf` / `anyOf`;
- `$ref` or recursive schema graphs;
- regular-expression constraints;
- arbitrary user-provided JSON Schema;
- provider-specific schema keywords.

The canonical representation is a Liquid type, not JSON Schema. Future adapters may render it into MCP JSON Schema or a provider-specific schema subset.

This matters because model/provider constrained-output implementations support different JSON Schema subsets. A bounded internal vocabulary lets Liquid validate the same contract independently from any particular provider.

### 8.4 Schema is descriptive validation, not authority

A `LuaValueSchema` can reject an invalid candidate early and can document the value shape for a model. It never replaces:

- current `World` access checks;
- the host-bound Lua capability closure;
- the real `LuaComponentCodec<T>::decode` function;
- transactional commit validation.

If schema metadata and the codec disagree, the trusted codec remains final. Such disagreement is a host/configuration bug that tests and diagnostics should expose.

Do not claim formal schema/codec equivalence for arbitrary C++ codec functions. L0 can verify known fixtures and every readable encoded snapshot that it emits.

---

## 9. Current milestone — L0: Model-Facing Lua Capability Contract

**Status:** Current / approved for implementation

### 9.1 Goal

Make an existing prepared Solid behavior's Lua capabilities machine-readable, bounded, and host-verifiable so a future model or agent can author valid lifecycle Lua without a provider integration or direct Runtime authority.

The smallest complete L0 flow is:

```text
trusted Lua codec + trusted LuaValueSchema
                  │
                  ▼
existing behavior permissions
                  │
                  ▼
          copied readable values
                  │
                  ▼
       immutable capability manifest
                  │
                  ▼
      host-side schema validation
```

There is no model call and no behavior activation flow in L0.

### 9.2 Required concepts

#### `LuaValueSchema`

A bounded recursive value-shape description matching current `LuaValue`.

It should support deterministic validation and bounded diagnostics. Its own construction must reject invalid/unbounded schema definitions.

#### Model-visible Lua codec registration

Add an additive registration path that associates trusted schema metadata with a `LuaComponentCodec<T>` exposed to models.

The existing `LuaBehaviorRunner::expose_component(type, scriptName, codec)` path must remain valid for existing v0.1 human-authored scripts and tests. Model discoverability may require a schema-bearing overload or adjacent API; the exact header shape is an implementation decision within L0.

A component exposed without model metadata remains executable by existing trusted/human script flows but is not automatically model-discoverable.

#### `LuaCapabilityManifest`

An immutable copied description produced for one real behavior and one current access revision.

For each capability it should contain enough information to author the current Lua API correctly:

- exact host-generated access path expression;
- script-visible component type name;
- component instance name;
- read/write permission;
- copied readable value when permitted;
- writable `LuaValueSchema` when permitted;
- bounded contract metadata needed by future renderers.

The manifest must not expose raw component slots, pointers, registries, mutable world objects, or a caller-selected behavior owner.

The exact representation of the world/behavior access revision may remain internal, but the host must be able to distinguish stale manifests when a later proposal is validated.

### 9.3 Exact permission behavior

For a current behavior capability:

| Permission | readable snapshot | writable schema |
|---|---:|---:|
| Read | yes | no |
| Write | no | yes |
| ReadWrite | yes | yes |

The manifest derives this from the same trusted `World` permission source used by Lua. It never infers permission from whether a snapshot exists.

### 9.4 Access-path rule

The host, not the model, produces the exact Lua expression for each capability.

Examples:

```text
access.Light.officeLight
access["Lighting Device"]["office-light"]
```

The future model prompt/tool response instructs the model to copy that path instead of reconstructing, normalizing, or escaping names itself.

### 9.5 Bounds

L0 must have explicit limits for schema and manifest construction. At minimum consider:

- schema nesting depth;
- properties per object;
- enum entries;
- array item count constraints;
- string/schema-description bytes if descriptions are included;
- total capabilities per manifest;
- total copied-value bytes;
- total manifest structural nodes.

Default values should be conservative and derived from existing Lua/`Value` limits where appropriate rather than introducing extremely large independent budgets.

### 9.6 Failure behavior

L0 must fail closed for model discovery:

- missing model-visible schema -> capability omitted or explicit host configuration error according to the chosen API contract;
- invalid schema definition -> registration fails;
- readable encoded value violates declared schema -> manifest build fails with bounded host diagnostic;
- behavior no longer exists -> manifest build fails;
- access changes -> rebuilt manifest reflects the new authority;
- stale manifest data never expands actual Lua authority because execution still checks the current host-bound capability and `World` permission.

### 9.7 L0 success cases

The test suite must demonstrate:

- valid Boolean/Integer/Number/String/Array/Object schemas;
- deterministic rejection of wrong kinds;
- integer/number range enforcement;
- string/array bounds;
- required-field enforcement;
- unknown object fields rejected by default;
- nesting/size limits;
- a real `Light{brightness}` codec whose declared `0..100` schema accepts valid values and rejects out-of-range values;
- readable snapshots are validated before entering a manifest;
- Read, Write, and ReadWrite produce exactly the allowed manifest fields;
- revoking access changes a newly built manifest;
- unusual type/component names generate the exact safe Lua path expression;
- manifest values are copies and do not provide retained component pointers;
- existing non-schema `expose_component` behavior remains source-compatible;
- a schema cannot grant authority that `World` denies;
- strict warning builds and the existing full test suite remain green.

### 9.8 L0 explicitly excludes

- OpenAI, Anthropic, llama.cpp, vLLM, Hermes, or any other model client;
- API keys, HTTP clients, provider routing, fallback, or billing metadata;
- prompt templates or autonomous repair loops;
- MCP server/client dependencies;
- behavior proposal persistence;
- automatic behavior activation;
- generic runtime-inspection APIs;
- arbitrary cross-behavior intent cancellation;
- a new behavior DSL/IR;
- a new adaptive trigger abstraction;
- real hardware, MQTT, voice, biosignals, or Liquid Layer policy;
- changes to Solid event format v1.

### 9.9 L0 allowed implementation area

The implementation should remain inside the existing Lua boundary rather than creating a speculative Stage 2 hierarchy or new exported target.

Expected files, subject to small API-shape adjustments during implementation:

```text
include/liquid/scripting/LuaValueSchema.hpp            # new
include/liquid/scripting/LuaCapabilityManifest.hpp     # new
include/liquid/scripting/LuaBehaviorRunner.hpp         # additive extension
src/scripting/LuaValueSchema.cpp                       # owner implementation
src/scripting/LuaCapabilityManifest.cpp                # owner implementation
src/scripting/LuaBehaviorRunner.cpp                    # owner implementation

tests/test_lua_schema.cpp                              # new
tests/test_lua_manifest.cpp                            # new

CMakeLists.txt
AGENTS.md
DEVELOPMENT_TRACKING.md
Liquid_Concepts_and_Architecture.md
docs/LIQUID_STAGE2_PLAN.md
docs/LIFECYCLE_SCRIPTING.md                            # only if public contract text changes
```

Do not add `include/liquid/adaptive/`, `src/adaptive/`, `Liquid::Adaptive`, an MCP folder, or provider folders in L0.

### 9.10 Coding-role split

Consistent with repository policy:

- Codex/agents may draft the public headers, tests, CMake entries, fixtures, diagnostics tables, and documentation;
- the project owner implements the substantive `.cpp` schema/manifest/runner logic unless explicitly delegating it.

---

## 10. Provisional milestone ladder after L0

Only L0 is implementation-authorized by this plan. Later milestones are deliberately provisional and must be re-evaluated using evidence from the previous milestone.

### L1 — Read-Only Liquid Runtime View

**Question answered:** Can a model reconstruct the relevant current truth of Solid without registry access or a shadow runtime?

Likely scope:

- immutable model-friendly behavior snapshots;
- owned live intents with name, target, value, priority, lifetime;
- current selection state and competing selected intent;
- authoritative observed external state where available;
- bounded recent causal facts;
- owner-thread snapshot construction and transport-safe copied values.

Required acceptance scenario: FocusSupport persistent `ON` loses to FollowUser `OFF`, remains alive, and becomes eligible to win again after the competitor disappears. A view must represent this accurately.

Do not add MCP yet unless L1's semantic API is already stable enough to expose cleanly.

### L2 — Behavior Proposal and Authoring Scope

**Question answered:** Can an external model propose a new behavior without receiving a live broadly privileged behavior merely to discover what it could do?

Likely scope:

- immutable `BehaviorProposal` containing exact lifecycle Lua source plus bounded review metadata;
- a trusted prospective authoring scope selected by the host/application;
- capability manifest for that scope;
- structural/source/capability validation;
- stale-scope rejection;
- explicit separation between new behavior proposal and revision of an approved behavior.

The model does not choose arbitrary permissions. Liquid Layer/host policy chooses the authoring scope; Liquid describes and enforces it.

### L3 — Deterministic Proposal Evaluation

**Question answered:** Can a candidate behavior be evaluated through the real Solid execution path before activation?

Likely scope:

- isolated prepared World/Runtime fixture;
- real `LuaLifecycleSystem` and `LuaBehaviorRunner`;
- real intent resolution;
- real `InMemoryAdapter` for effects;
- evaluation report separating proposed intents, selected desires, commands, reports, and final observed state;
- deterministic fixtures and failure cases.

The current app-level `SimulationScenario` is light-specific. Do not pretend it is already a generic proposal evaluator; L3 should earn that abstraction from actual proposal tests.

### L4 — MCP Adapter

**Question answered:** Can an external agent runtime safely consume the same Liquid semantic API?

Likely first surface:

- focused discovery/capability tools;
- read-only inspection tools;
- behavior validation/evaluation tools;
- no unrestricted mutators.

Protocol direction:

- target MCP `2026-07-28` or the current stable revision when implementation begins;
- do not build new code around deprecated MCP Sampling;
- keep MCP input/output schemas as renderings of Liquid's own semantic contracts;
- expose small focused tool sets rather than thousands of low-level engine calls;
- validate all arguments server-side; MCP descriptions/annotations are not authority.

Implementation-language decision is intentionally deferred. As of August 2026 the official MCP SDK list has Tier 1 TypeScript/Python/C#/Go and Tier 2 Java/Rust/Ruby, with no official C++ SDK. The existing Solid Scope bridge also demonstrates that an owner-operated sidecar can preserve the one-Runtime rule. Re-evaluate SDK maturity at L4 rather than adding a community C++ MCP dependency to Liquid now.

Hermes should be one integration proof, not the contract owner. At least one second MCP client or conformance-oriented test should prove that the server is not Hermes-specific.

### L5 — Approval, Activation, and Bounded Operations

**Question answered:** Can reviewed model-produced behavior become live without source substitution or authority expansion?

Likely scope:

- approval bound to exact proposal/source hash, scope/access revision, and evaluation evidence;
- installation of the exact approved lifecycle revision;
- explicit revision workflow for modifying an approved script;
- bounded operational actions justified by real scenarios;
- ownership-aware cancellation/removal semantics;
- no silent source mutation due to context changes.

### L6 — Liquid Proposal Evidence and Change Surface

**Question answered:** Can the system explain why a behavior was proposed/approved and give applications bounded change information without contaminating Solid's deterministic event contract?

Likely scope:

- separate Liquid proposal/evaluation/approval records;
- correlation to Solid session/behavior/runtime evidence;
- optional provider/model metadata supplied by the application that performed inference;
- privacy-aware recording of exactly what context was shared;
- bounded change cursor/feed only if Liquid Layer scenarios prove it necessary.

Liquid's records answer:

> Why was this behavior proposed, evaluated, revised, approved, or rejected?

Solid's records continue to answer:

> What did the deterministic runtime actually execute and observe?

---

## 11. Rejected or deferred alternatives

### 11.1 LLM inside `Runtime::run_frame()`

Rejected. It violates deterministic frame ownership, latency expectations, failure isolation, replay semantics, and owner-thread design.

### 11.2 Liquid-owned provider abstraction as the first Stage 2 feature

Rejected for now. Liquid Layer is the correct place to choose local/hosted inference and direct/agent execution. Liquid only needs to expose a stable model-facing surface.

If a future reusable Liquid capability truly requires engine-owned inference independent of any application, that need must be demonstrated before adding a provider port.

### 11.3 Hermes as a Liquid dependency

Rejected. Hermes is a useful external agent runtime and MCP client. Approved behaviors must continue executing with Hermes absent, and another agent must be able to consume the same Liquid surface.

### 11.4 MCP as the internal Liquid API

Rejected. MCP is a protocol/transport adapter. The semantic API should be testable without serialization or network transport, then exposed over MCP.

### 11.5 Full JSON Schema as Liquid's canonical value schema

Rejected for L0. Provider/model constrained-output implementations differ, and current `LuaValue` is much smaller than JSON. Liquid should own a bounded schema vocabulary and render outward formats later.

### 11.6 New declarative behavior IR before Lua evidence

Deferred. Lua is already the sandboxed executable behavior representation designed for generated behavior. A separate IR should be introduced only if real proposal/evaluation evidence demonstrates repeated problems with Lua authoring, static analysis, reviewability, or portability.

### 11.7 `SemanticTrigger` or similar core adaptive state machine

Deferred. Existing components, lifecycle watches, intent lifetimes, named intent cancellation/replacement, and application orchestration already cover many cases. Introduce a reusable adaptive trigger abstraction only after several distinct scenarios require the same missing primitive.

### 11.8 Continuous notifications to an agent whenever its intent loses

Rejected as a default. A live intent losing selection is ordinary Solid behavior, not necessarily an exception. Liquid should let a caller inspect current truth when invoked; Liquid Layer decides which changes are worth waking an agent for.

---

## 12. Privacy and security principles for later milestones

These apply even before a network transport exists.

- Context is allowlisted, not dumped wholesale.
- A model sees only capabilities selected for the current task/scope.
- A readable snapshot does not imply write permission.
- Raw slots, pointers, registry internals, credentials, adapter configuration, unrelated users, and full event history are excluded by default.
- Generated scripts never receive more authority during a repair attempt merely because a previous attempt failed.
- External agent memory is not trusted as current runtime truth.
- Remote transport identity or MCP tool annotations do not replace Solid permission checks.
- Model-facing structures and diagnostics are bounded to resist accidental or adversarial amplification.
- Future proposal evidence should record which context was actually shared, subject to explicit privacy policy.

---

## 13. Research notes and external precedents

Research was reviewed against current sources on 28 August 2026. These sources support implementation choices; they do not override Solid's repository contracts.

### Model Context Protocol

- MCP `2026-07-28` moves the core protocol to stateless request/response, deprecates Sampling/Roots/Logging, and makes tool schemas full JSON Schema 2020-12. New servers should not depend on deprecated Sampling for model access.
  - https://blog.modelcontextprotocol.io/posts/2026-07-28/
  - https://blog.modelcontextprotocol.io/posts/2026-07-28-release-candidate/
- Official SDK tiers currently list TypeScript, Python, C#, and Go as Tier 1; Java, Rust, and Ruby as Tier 2; no official C++ SDK is listed.
  - https://github.com/modelcontextprotocol/modelcontextprotocol/blob/main/docs/docs/2026-07-28/sdk.mdx

### Home Assistant

Home Assistant provides a strong architectural precedent: integrations register focused LLM APIs, and those semantic APIs can later be automatically exposed over MCP. The built-in Assist API limits the model to explicitly exposed entities/capabilities rather than giving administrative access.

- https://developers.home-assistant.io/docs/core/llm/
- https://www.home-assistant.io/blog/2024/06/07/ai-agents-for-the-smart-home/

The lesson for Liquid is not to copy Home Assistant's domain API. It is to keep the semantic model-facing contract independent of transport and deliberately small.

### Hermes Agent

Hermes is an appropriate future integration target because it:

- supports external MCP servers with per-server tool filtering;
- supports custom/OpenAI-compatible inference providers;
- distinguishes full agent execution from direct structured LLM calls available to plugins.

- https://hermes-agent.nousresearch.com/docs/user-guide/features/mcp/
- https://hermes-agent.nousresearch.com/docs/developer-guide/adding-providers
- https://hermes-agent.nousresearch.com/docs/developer-guide/plugin-llm-access

This reinforces the Stage 2 decision that direct inference and agent execution are different application choices over one Liquid surface.

---

## 14. Milestone advancement rule for Stage 2

For each Liquid milestone:

1. start from current `main` on a short-lived branch;
2. restate the single missing capability the milestone closes;
3. identify which existing Solid facilities are reused before adding new abstractions;
4. define public/header shape and failure behavior before core implementation;
5. write success and adversarial tests;
6. keep model/provider/network dependencies out unless that milestone explicitly exists to integrate them;
7. run the strict full suite and relevant sanitizer/consumer checks;
8. record completion evidence;
9. re-evaluate the next provisional milestone instead of automatically expanding it;
10. update `AGENTS.md` and `DEVELOPMENT_TRACKING.md` before widening scope.

A new abstraction should answer at least one demonstrated requirement that cannot be solved cleanly with existing Solid/Liquid primitives. Folder creation follows accepted milestones, not architectural imagination.

---

## 15. Definition of Stage 2 success

Liquid is successful when a Liquid Layer application can choose any suitable model/agent strategy and, through Liquid:

- discover the exact bounded authority available for behavior authoring;
- create and validate deterministic Lua behavior proposals;
- evaluate them through the real Solid execution path;
- inspect current behaviors/intents/resolution/observed truth without internal registry access;
- activate/revise/cancel only through explicit bounded operations;
- correlate adaptive decisions with deterministic runtime evidence;
- replace or remove the external model/agent without invalidating already approved Solid behaviors.

The ultimate design test remains:

> If every LLM and agent is turned off, already approved behaviors continue to execute, lose and regain intent resolution, expire/cancel according to deterministic policy, dispatch effects, validate feedback, and preserve evidence under Solid alone.
