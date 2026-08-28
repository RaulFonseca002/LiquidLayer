# Liquid Stage 2 Plan

**Status:** Owner-approved Stage 2 architecture and implementation plan  
**Date:** 28 August 2026  
**Foundation:** Solid v0.1.0  
**Current implementation milestone:** L0 — Model-Facing Lua Capability Contract

---

## 1. Purpose

This document closes the Stage 2 architecture research phase and defines the implementation direction for **Liquid**.

Liquid is not an LLM provider, an agent runtime, a conversation system, or a smart-home application. It is the reusable **model-facing control and authoring layer over Solid**: the surface that lets an external model or agent understand, author, inspect, validate, evaluate, and eventually perform bounded operations over Solid behaviors without bypassing Solid's deterministic authority.

The final **Liquid Layer** application chooses the intelligence, decides when it should run, selects the user/environment context, and gives that context application meaning. A local model, hosted model call, Hermes Agent, or another future agent runtime may consume the same Liquid surface.

```text
                           LIQUID LAYER
        application policy / sensors / user context / AI routing
             ┌──────────────┼───────────────┐
             │              │               │
        local model     hosted model    agent runtime
             │              │            e.g. Hermes
             │              │               │
             │              │              MCP
             └──────────────┴────────┬──────┘
                                     ▼
                                  LIQUID
                     discover / author / observe
                     validate / evaluate / operate
                        model-friendly evidence
                                     │
                                     ▼
                                   SOLID
                    deterministic execution authority
```

The implementation must grow only when a real Liquid Layer scenario demonstrates a capability that the existing Solid/Liquid boundary does not already provide.

---

## 2. Architectural Thesis

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

Likewise:

```text
intent alive
    ≠ intent currently selected
```

A persistent intent that loses resolution remains alive until normal expiration, cleanup, or explicit cancellation removes it. When a competitor disappears, that older intent may become selected again without being recreated and without invoking a model.

This is a core property Liquid must expose accurately rather than hiding behind a simplified "current state" abstraction.

### 2.2 Lua remains the generated executable behavior boundary

Solid's Lua lifecycle boundary was deliberately designed for generated behavior:

- every execution has a host-fixed `BehaviorId` owner;
- only explicitly exposed typed/named capabilities are visible;
- readable values are copied snapshots;
- writable capabilities create immutable intents through host-bound closures;
- named proposals, owner-scoped cancellations, and watches commit transactionally;
- each behavior receives a fresh Lua VM every frame;
- persistent script state lives in ordinary codec-backed components;
- source, instructions, memory, host values, strings, tables, diagnostics, created intents, cancellations, and watches are bounded;
- scripts receive no `World`, registries, `Coordinator`, raw slots, component pointers, owner selection, OS/I/O/package/debug authority, or arbitrary native execution.

Therefore Stage 2 does **not** introduce another generated behavior language before evidence proves one is necessary. The first generated executable artifact remains an exact `LuaBehaviorScript` revision using the existing lifecycle contract.

Changing environment context does not silently rewrite an approved script. A source change is an explicit behavior revision.

### 2.3 Liquid owns a semantic API, not an AI runtime

Liquid should eventually provide model-friendly capabilities to:

- discover what a behavior may read/write;
- describe exact script-visible value shapes and bounded trusted semantics;
- obtain copied readable snapshots;
- validate generated lifecycle Lua against the actual host contract;
- evaluate proposals through the real Solid simulation path;
- inspect behaviors, live intents, current selections, commands/reports, and observed truth;
- perform explicitly bounded operations that preserve ownership and authority;
- expose structured causal evidence suitable for external explanation.

Liquid should **not** decide:

- which model/provider to use;
- local versus hosted inference;
- direct model call versus agent runtime;
- when a model should be invoked;
- what `focus`, `sensory overload`, `task initiation`, or other application concepts mean;
- which sensor/event should wake an agent;
- how conversational or long-term user memory works.

Those are Liquid Layer responsibilities.

### 2.4 MCP is a transport adapter over Liquid

MCP is not Liquid's domain model.

```text
Liquid semantic API
      │
      ├── direct application/in-process use
      │
      └── future MCP adapter
              │
              ├── Hermes Agent
              ├── another MCP client
              └── future agent runtimes
```

The semantic API must be testable without MCP serialization/networking. This keeps Liquid independent from protocol revisions and lets direct model calls use the same contracts without pretending to be an agent.

---

## 3. Direct Inference, Agents, Local Models, and Hosted Models

Two independent axes must stay separate.

### 3.1 Reasoning style

```text
Direct inference
    bounded context -> one model response

Agent execution
    goal -> model/tool/model/... -> result/action
```

Direct inference is appropriate for bounded classification, interpretation, extraction, or other one-shot structured tasks.

An agent runtime is appropriate when work benefits from multiple tool calls, iterative repair, conversation, persistent memory, scheduling, or longer-running orchestration.

### 3.2 Deployment

Either reasoning style may use a local or hosted model:

```text
Direct + Local
Direct + Hosted
Agent + Local
Agent + Hosted
```

Liquid does not choose among these. Liquid Layer does.

Hermes currently demonstrates why these concepts should remain separate: it has a full agent loop and separately gives plugins bounded one-shot `complete` / `complete_structured` model calls. It can also route to self-hosted/OpenAI-compatible inference providers. Hermes is therefore a useful future Liquid consumer, not a Liquid provider abstraction or dependency.

### 3.3 Model memory is not runtime truth

A caller may be stateless between calls. The system must not be.

Agent/conversation memory may preserve meaning, preferences, or history. Current runtime facts must be reconstructed from Liquid/Solid whenever needed.

A newly started agent or a different provider should be able to inspect the same current Solid truth and continue correctly.

---

## 4. What Liquid Layer Must Eventually Be Able to Do Through Liquid

These capabilities define the Stage 2 target surface. They do **not** authorize implementing all of them in L0.

### 4.1 Discover

Within an explicitly selected scope, a caller should be able to learn:

- which component capabilities are available;
- their exact Lua access expressions;
- current read/write/read-write permission;
- exact model-visible value shape;
- bounded trusted semantic descriptions/units where useful;
- copied current values for readable capabilities;
- current monotonic `now_ms` when exposed for authoring;
- the stable Lua authoring-contract version and relevant execution limits.

A copied snapshot is data, never authority.

### 4.2 Author

A caller should eventually be able to propose an exact Lua lifecycle behavior without direct world mutation authority.

The trusted host must be able to:

- select the prospective authoring scope;
- preserve the exact candidate source;
- validate source/shape/capability use;
- reject stale authoring context;
- evaluate the candidate through the real sandbox/simulator;
- distinguish a new behavior from a revision of an approved one.

The model does not choose arbitrary permissions.

### 4.3 Observe

A caller should eventually reconstruct current relevant truth without reading registries or raw event files.

A model-friendly view should be able to answer, when relevant:

- what behavior is being inspected;
- which live intents it owns;
- each intent's stable name, requested value, target, priority, and lifetime;
- whether each live intent is currently selected;
- which competing intent is selected for the target;
- which behavior owns that selected intent;
- authoritative observed external state where available;
- relevant command/report state;
- bounded recent causal evidence.

Liquid composes this view from Solid sources of truth. It does not keep a parallel shadow runtime.

### 4.4 Validate and evaluate

Generated behavior must be treated as untrusted candidate data regardless of provider features such as JSON mode, constrained decoding, tool calling, or structured outputs.

The intended path is:

```text
candidate artifact
    -> Liquid structural/schema/capability validation
    -> actual Lua sandbox/codec validation
    -> deterministic Solid evaluation scenarios
    -> structured evidence
```

Provider-side schema adherence improves generation quality; it is not an authority boundary.

Simulation can demonstrate mechanical validity and scenario behavior. It cannot prove that an intervention is desirable or appropriate for a person.

### 4.5 Operate

Some later scenarios need a caller to stop or alter live operation without rewriting approved source.

Any operation must be explicitly scoped and ownership-aware. Liquid must never expose generic primitives equivalent to:

```text
mutate_world
write_any_component
destroy_any_intent
raw_registry_access
execute_arbitrary_native_code
```

The exact mutation surface is deferred until read-only inspection and proposal/evaluation demonstrate the operations actually required.

### 4.6 React

Liquid Layer needs reactive behavior, but Liquid does not decide what changes are semantically important.

Liquid should eventually make bounded state/change evidence available so an application can decide, for example:

```text
presence changed          -> maybe invoke local inference
intent lost resolution    -> maybe ignore
repeated effect failures  -> maybe wake an agent
explicit user request     -> invoke an agent now
```

Do not add `SemanticTrigger`, `BehaviorCondition`, or equivalent adaptive core abstractions until several distinct scenarios demonstrate the same missing engine primitive after existing components, Lua watches, named intents, intent lifetimes, and application orchestration have been tried.

### 4.7 Explain

Liquid should expose structured causal facts rather than generate natural-language explanations itself.

Example facts:

```text
observed office light = OFF
selected desire       = OFF from FollowUser
FocusSupport wants    = ON, persistent, alive, not selected
OFF command           = applied and confirmed
```

The external caller/model may turn those facts into language.

---

## 5. Architectural Acceptance Scenarios

These scenarios are tests of the abstraction, not hard-coded Liquid concepts.

### 5.1 FocusSupport with deterministic continuation

A prepared `FocusSupport` Lua behavior owns a persistent light intent while focus support is relevant.

If a temporary absence should not immediately end support, the approved behavior can encode deterministic policy through existing named intent replacement and/or an `UntilTime` intent. Once the policy exists, a model does not need to supervise every frame or minute.

If evidence later shows the approved source itself should change, that is an explicit revision rather than implicit runtime adaptation.

### 5.2 FollowUser conflicts with FocusSupport

A second behavior follows the user through the house and wants an abandoned light OFF.

If FollowUser wins:

```text
FocusSupport intent ON: alive
FocusSupport intent ON: not selected
FollowUser intent OFF: selected
observed light: OFF after authoritative feedback
```

If FollowUser later stops wanting OFF, the original persistent FocusSupport intent may win again without reconstruction or a model call.

A later Liquid runtime-view milestone must represent this accurately.

### 5.3 Stateless caller reconstruction

An agent process may restart, switch provider/model, or lose conversation context. A fresh caller should reconstruct current relevant runtime truth through Liquid rather than rely on what it remembers having requested.

### 5.4 Two AI strategies, one Liquid surface

The same semantic Liquid contracts should support both:

- a direct local structured model call chosen by Liquid Layer; and
- an external agent runtime such as Hermes through MCP.

Authority rules must not depend on the caller type.

---

## 6. Frozen Solid Constraints for Every Liquid Milestone

Every Stage 2 implementation must preserve all of the following.

1. `Runtime` remains the sole frame-phase driver.
2. `World` remains the public state/behavior/component lifecycle boundary.
3. Registries, `Coordinator`, `WorldState`, raw slots, borrowed pointers, and mutable internal state are not exposed to models or remote transports.
4. Generated executable behavior continues through the existing Lua sandbox unless a later milestone explicitly proves a replacement is required.
5. Intent semantic contents remain immutable after creation.
6. A losing live intent remains live until normal expiration/cleanup/cancellation removes it.
7. Selected desire remains distinct from command, report, and observed physical truth.
8. External state is not optimistically mutated from selection.
9. Model/agent calls do not block `Runtime::run_frame()`.
10. Solid Event Format v1 is not casually expanded with prompts, conversations, proposal rationales, or provider metadata.
11. `World` and `Runtime` owner-thread confinement is preserved.
12. Existing v0.1 source compatibility is not weakened merely to make model integration convenient.
13. Model/provider structured-output features never replace local host validation.

---

## 7. Threading, Snapshot, and Transport Rules

Solid confines `World` and `Runtime` to their creating thread. Future HTTP/MCP implementations are naturally concurrent/event-driven, so a transport must not retain `Runtime&` and call it from arbitrary request threads.

Accepted direction:

> Model-facing data is produced through the trusted owner-thread boundary as bounded immutable copies. Mutating requests are marshalled back to the host/Runtime owner-controlled path. A transport owns no Runtime authority of its own.

L0 only constructs immutable capability data on the owner thread.

L1 must explicitly define consistency semantics for runtime snapshots. Combining "live intents now" with an unrelated old `last_frame_log()` selection would create a misleading view if topology/intents changed between those moments. A future runtime view must therefore correspond to a clearly defined capture point, likely an owner-thread snapshot associated with a completed frame or another explicit host-defined consistency boundary.

The exact snapshot publisher, request queue, sidecar IPC, or mutation-command bridge is deferred until a milestone actually requires asynchronous transport.

---

## 8. Trust and Prompt/Data Boundary

The model-facing surface must distinguish trusted contract metadata from dynamic data.

### Trusted registration metadata

May include bounded text intentionally authored by the host developer, such as:

- component/capability description;
- field meaning;
- units;
- enum meaning;
- stable authoring-contract text/version.

This metadata is configuration and must have explicit byte/count limits.

### Dynamic/untrusted data

Includes:

- component snapshots;
- component instance names from integrations;
- sensor/user text;
- event/evidence text;
- adapter/device-provided strings;
- model-produced source or metadata.

Future renderers should encode these as structured data or clearly delimited data, not interpolate them into trusted instructions and hope the model distinguishes them.

This is not a claim that prompt injection can be eliminated. It prevents the Liquid contract itself from accidentally treating arbitrary runtime strings as instructions.

---

## 9. Schema Strategy

### 9.1 Why a model-facing schema is required

`LuaComponentCodec<T>` currently contains executable C++ `encode` and `decode` functions. A model cannot introspect them to learn that a field is required, an integer has a range, or a string belongs to a finite enum.

A copied value also cannot communicate authority: seeing a value never implies permission to write it.

Therefore every **model-discoverable Lua binding** needs trusted machine-readable shape metadata beside the executable codec.

### 9.2 Do not overload Solid `ComponentSchema`

Solid's `ComponentSchema` identifies a component's stable schema name/version for codec/compatibility purposes. It currently does not describe script-visible fields/ranges.

Stage 2 should not silently change that meaning.

The provisional L0 type is **`LuaValueSchema`** because it describes the value vocabulary actually transported by `LuaBehaviorRunner`.

### 9.3 V1 vocabulary

The canonical schema is deliberately smaller than full JSON Schema and mirrors current `LuaValue`:

- Boolean;
- signed Integer with optional inclusive bounds;
- finite Number with optional inclusive bounds;
- String with explicit length bounds and optional finite enum;
- Array with one item schema and item-count bounds;
- Object with named fields and required/optional status, rejecting unknown fields by default.

Short bounded trusted descriptions may be attached where useful to explain meaning/units. Description text does not alter validation semantics.

Do not add in L0 unless a real current codec proves it necessary:

- null;
- bytes;
- unsigned-only integer semantics;
- unions / `oneOf` / `anyOf` / `allOf`;
- `$ref`, `$defs`, or recursive schema graphs;
- regex constraints;
- arbitrary user-provided JSON Schema;
- provider-specific schema keywords.

The canonical representation is a Liquid type. Future adapters may render it to MCP JSON Schema 2020-12 or provider-specific structured-output subsets.

### 9.4 Schema is validation metadata, not authority

A `LuaValueSchema` may reject malformed candidate values early and document the shape for a model. It never replaces:

- current `World` access checks;
- the host-bound Lua capability closure;
- the actual `LuaComponentCodec<T>::decode` function;
- transactional intent commit validation.

If declared schema and executable codec disagree, the executable trusted codec remains final. The disagreement is a host/configuration bug that tests/diagnostics should expose.

Do not claim formal equivalence between arbitrary C++ codec functions and declarative schema metadata.

---

# 10. Current Milestone — L0: Model-Facing Lua Capability Contract

**Status:** Current / approved for implementation.

## 10.1 Goal

Make an existing prepared Solid behavior's Lua capabilities machine-readable, bounded, and host-verifiable so a future model/agent can author against the real Lua API without adding a provider integration, MCP transport, direct Runtime authority, or behavior activation flow.

Smallest complete L0 circuit:

```text
trusted Lua codec + trusted LuaValueSchema/metadata
                  │
                  ▼
prepared behavior's current World permissions
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

There is no inference call in L0.

## 10.2 Required concepts

### `LuaValueSchema`

A bounded recursive value-shape description matching current `LuaValue`.

It must support deterministic validation and bounded diagnostics. Invalid or unbounded schema definitions must be rejected during construction/registration rather than later during model discovery.

Likely validation concerns:

- valid min/max ordering;
- finite numeric bounds;
- string bounds;
- enum count/value bounds and duplicate handling;
- array min/max ordering;
- object field-count limits;
- unique field names;
- recursive depth/node limits;
- bounded description bytes.

The exact public C++ representation should be chosen for simple ownership and validation, not clever metaprogramming.

### Model-visible Lua codec registration

Add an **additive** registration path associating trusted schema/semantic metadata with a `LuaComponentCodec<T>` binding.

The existing:

```cpp
runner.expose_component(type, scriptName, codec);
```

must remain source-compatible for existing v0.1 trusted/human scripts and tests.

A schema-bearing overload or adjacent API is acceptable. A component exposed without model metadata remains executable by existing Lua flows but is not automatically model-discoverable.

### `LuaCapabilityManifest`

An immutable copied description for one real prepared behavior and its current access state.

It should contain enough information for a future renderer/model to author the current Lua API correctly, including:

- exact host-generated Lua access expression;
- script-visible type name;
- component instance name;
- bounded trusted capability/type/field descriptions where registered;
- exact permission projection;
- copied readable value when permitted;
- writable `LuaValueSchema` when permitted;
- current monotonic `now_ms` when the manifest is captured;
- a stable Lua authoring-contract/version marker;
- bounded execution/authoring metadata needed by future renderers.

The manifest must not expose:

- raw component slots;
- component pointers/references;
- registries/Coordinator/WorldState;
- mutable `World`/`Runtime` references;
- a caller-selected owner;
- credentials or adapter configuration.

The host should retain enough internal capture identity (world/behavior/access revision or equivalent) to identify stale data in later milestones, but raw runtime handles do not need to be rendered to a model merely because the host uses them internally.

## 10.3 Exact permission projection

| Current permission | readable snapshot | writable schema |
| --- | ---: | ---: |
| Read | yes | no |
| Write | no | yes |
| ReadWrite | yes | yes |

Permission is derived from the same current trusted `World` state used by Lua execution. It is never inferred from whether a snapshot happens to exist.

A model schema cannot grant a capability that `World` denies.

## 10.4 Access-path rule

The host generates the exact Lua expression.

Examples:

```text
access.Light.officeLight
access["Lighting Device"]["office-light"]
```

The future renderer/model contract instructs the model to copy this expression rather than normalize, reconstruct, or escape names itself.

Path escaping must be deterministic and tested against quotes, backslashes, control characters, keywords, numeric-leading names, Unicode, and other names that cannot safely use dot notation.

## 10.5 Bounds

L0 must define explicit construction limits, at minimum for:

- schema nesting depth;
- schema structural nodes;
- fields per object;
- enum entries and total enum string bytes;
- description bytes per field/capability and total;
- string/array declared bounds;
- capabilities per manifest;
- copied readable-value bytes/nodes;
- total manifest structural size;
- diagnostic bytes.

Defaults should be conservative and harmonized with existing `LuaExecutionLimits` / `ValueLimits` where appropriate rather than inventing giant independent budgets.

## 10.6 Failure behavior

Model discovery fails closed.

- invalid schema definition -> registration/configuration fails;
- missing model metadata -> existing Lua execution may remain available, but the binding is not model-discoverable;
- readable encoded value violates declared schema -> manifest construction fails with bounded host diagnostic;
- behavior does not exist -> manifest construction fails;
- access changes -> a newly constructed manifest reflects new authority;
- stale manifest data never widens actual execution authority because Lua execution still checks the current binding/World access;
- malformed dynamic names/text remain data and are escaped/structured rather than treated as instructions.

## 10.7 L0 implementation order

1. Write schema construction/value-validation tests first.
2. Draft the smallest public `LuaValueSchema` header that satisfies those tests.
3. Implement schema validation/limits.
4. Add real codec fixtures, including `Light{brightness: 0..100}`.
5. Write capability-manifest permission/copy/path/version/time tests.
6. Draft `LuaCapabilityManifest` and the additive schema-bearing binding API.
7. Implement manifest construction from the same trusted bindings/current permissions/snapshots used by Lua.
8. Add stale/revoked-access, unusual-name, bounds, and malformed-metadata cases.
9. Update CMake and public scripting documentation only for the API actually implemented.
10. Run the strict full suite and relevant sanitizer/consumer verification.

## 10.8 L0 success evidence

At minimum demonstrate:

- valid Boolean/Integer/Number/String/Array/Object schemas;
- deterministic rejection of wrong kinds;
- integer/number range enforcement;
- string/array bounds;
- required/optional field enforcement;
- unknown object fields rejected by default;
- schema definition depth/node/field/enum/description bounds;
- invalid schema definitions rejected before use;
- a real `Light{brightness}` Lua codec whose declared `0..100` schema accepts valid snapshots and rejects invalid values;
- every readable snapshot entering a manifest validates against registered schema;
- Read/Write/ReadWrite produce exactly the permitted manifest fields;
- revoked/removed access changes a rebuilt manifest;
- exact safe Lua path expressions for unusual names;
- manifest captures `now_ms` and authoring-contract version deterministically;
- manifest values/metadata are copies and contain no retained component pointers/registries;
- existing schema-less `expose_component(...)` remains source-compatible;
- schema/metadata cannot enlarge actual Lua/World authority;
- diagnostics and manifest construction stay bounded;
- strict warnings-as-errors and all existing tests remain green.

## 10.9 L0 explicitly excludes

- OpenAI, Anthropic, llama.cpp, vLLM, Hermes, or any model client;
- API keys, HTTP clients, provider routing, fallback, token/cost metadata;
- generic prompt builder or autonomous repair loop;
- MCP server/client dependencies;
- behavior proposal persistence;
- automatic behavior activation/approval;
- generic runtime-inspection API;
- arbitrary cross-behavior intent cancellation;
- new behavior DSL/IR;
- `SemanticTrigger`/adaptive state-machine abstraction;
- real hardware, MQTT, voice, biosignals, or Liquid Layer policy;
- changes to Solid Event Format v1;
- speculative new `Liquid::Adaptive` target/folders.

## 10.10 Allowed implementation area

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
README.md
docs/LIQUID_STAGE2_PLAN.md
docs/LIFECYCLE_SCRIPTING.md                            # only if public Lua contract changes
```

Small API/file adjustments inside the existing `scripting/` boundary are allowed when tests show a cleaner minimal design. Do not create provider/agent/MCP/adaptive directory hierarchies in L0.

## 10.11 Coding-role split

Consistent with repository policy:

- Codex/agents may draft public headers, tests, CMake entries, fixtures, diagnostics tables, and docs;
- the project owner implements substantive `.cpp` schema/manifest/runner logic unless explicitly delegating it.

---

# 11. Provisional Milestone Ladder After L0

Only L0 is currently implementation-authorized. Every later milestone must be re-evaluated from evidence produced by the previous one.

## L1 — Read-Only Liquid Runtime View

**Question:** Can an external caller reconstruct relevant current Solid truth without registry access, shadow state, or conversation memory?

Likely scope:

- immutable owner-thread-built snapshots;
- scoped/opaque behavior references suitable for API/model use;
- owned live intents with name, encoded value, target, priority, lifetime;
- selected/not-selected distinction;
- selected competitor/owner when applicable;
- authoritative observed external state where available;
- bounded command/report/evidence context;
- explicit snapshot consistency/frame identity.

Known gaps that L1 must solve rather than paper over:

- generic stable type/component naming for `ComponentTarget` without exposing `ComponentRegistry`;
- generic mapping from externally controlled component targets to effect route/target/observed state without exposing Runtime internals;
- exact consistency semantics between live intent state and the frame selection being reported;
- API-safe references that do not pretend world-local generational handles are permanent historical IDs.

Required acceptance scenario: FocusSupport persistent `ON` loses to FollowUser `OFF`, remains alive, and can become selected again after the competitor disappears. A captured view must represent each fact correctly.

Do not add MCP merely to make L1 feel integrated. Stabilize the semantic read model first.

## L2 — Behavior Proposal and Prospective Authoring Scope

**Question:** Can an external model propose a new behavior without receiving a broadly privileged live behavior merely to discover what it could do?

Likely scope:

- immutable `BehaviorProposal` containing exact lifecycle Lua source plus bounded review metadata;
- trusted prospective authoring scope selected by the host/application;
- capability manifest for that scope;
- source/capability/contract-version validation;
- stale-scope rejection;
- explicit distinction between new behavior proposal and approved-behavior revision.

The model never chooses arbitrary permissions.

L2 must decide how a prospective scope relates to Solid behavior identity. Possible implementations include a prepared non-executing behavior or a separate host-owned scope representation, but the choice should follow L0/L1 evidence rather than be pre-decided now.

Dynamic/user/environment text remains structured input data. Stable authoring instructions and trusted schema descriptions remain separate from that data.

## L3 — Deterministic Proposal Evaluation

**Question:** Can a candidate behavior be exercised through the real Solid path before activation?

Likely scope:

- isolated prepared `World`/`Runtime` fixture;
- real `LuaLifecycleSystem` and `LuaBehaviorRunner`;
- real intent lifetimes and resolution;
- real `InMemoryAdapter` for effects;
- deterministic scenario inputs;
- evaluation report separating proposed intents, selected desires, commands, reports, observations, and final authoritative state;
- bounded diagnostics suitable for an external repair loop.

The current app-level `SimulationScenario` is light-specific. Do not claim it is already a generic behavior evaluator; L3 should extract/generalize only what real proposal tests require.

## L4 — MCP Adapter

**Question:** Can external agent runtimes consume the same Liquid semantic API without gaining authority?

Likely first surface:

- focused capability/discovery tools;
- read-only runtime-inspection tools;
- behavior validation/evaluation tools;
- no unrestricted mutators.

Protocol direction based on research current on 28 August 2026:

- implement against the current stable MCP revision at milestone start; current stable revision is `2026-07-28`;
- the 2026 core is stateless; do not invent Liquid semantics around MCP session state;
- do not build new code around deprecated MCP Sampling, Roots, or Logging;
- model invocation remains a client/application concern rather than MCP server Sampling;
- MCP tool input/output schemas are renderings of Liquid contracts, not canonical engine types;
- define structured output schemas where useful and validate server results locally;
- keep tools few, focused, and context-scoped rather than mirror low-level C++ APIs;
- tool descriptions/annotations are hints, not authorization;
- preserve Solid owner-thread confinement through copied snapshots and owner-controlled mutation requests;
- start with local/owner-controlled deployment unless a real remote requirement justifies OAuth/network exposure;
- if remote authorization is later required, follow MCP audience/token-isolation rules and never pass inbound client tokens through to downstream services.

SDK/language choice remains deferred. As of 28 August 2026 official Tier 1 MCP SDKs are TypeScript, Python, Go, and C#, and no official C++ SDK is listed. Re-evaluate SDK maturity at L4 rather than adding a community C++ dependency now.

Hermes is an integration proof, not the contract owner. Current Hermes documentation supports MCP protocol-era negotiation including the `2026-07-28` stateless probe. At least one second client or conformance-oriented test should prove the Liquid MCP surface is not Hermes-specific.

## L5 — Approval, Activation, Revision, and Bounded Operations

**Question:** Can reviewed model-produced behavior become live, be revised, or be stopped without source substitution, stale approval, or authority expansion?

Likely scope:

- approval bound to exact proposal/source hash;
- approval bound to the authoring scope/access revision and evaluation evidence;
- installation of the exact approved lifecycle source/revision;
- explicit behavior revision workflow;
- ownership-aware cancellation/removal operations justified by actual scenarios;
- no generic arbitrary world/intent mutation.

Critical revision question:

> What happens to persistent intents owned by the behavior's previous approved script revision?

Changing `LuaBehaviorScript.source/revision` alone resets lifecycle execution state but does not by itself prove the semantic fate of persistent intents that already exist. L5 must define this intentionally before claiming safe revision/activation semantics.

Context changes that merely make a behavior temporarily less relevant must not silently mutate its approved source.

## L6 — Liquid Proposal Evidence and Change Surface

**Question:** Can adaptive decisions be audited, and can applications learn about bounded changes, without contaminating Solid's deterministic evidence contract?

Likely scope:

- Liquid proposal/evaluation/approval/revision records separate from Solid Event Format v1;
- correlation to Solid session/behavior/runtime evidence;
- optional provider/model metadata supplied by the application that actually performed inference;
- privacy-aware record of which context/capabilities were shared;
- bounded change cursor/feed only if Liquid Layer scenarios demonstrate it is necessary.

Liquid evidence answers:

> Why was this behavior proposed, evaluated, revised, approved, or rejected?

Solid evidence continues to answer:

> What did the deterministic runtime actually execute and observe?

---

# 12. Rejected or Deferred Alternatives

### LLM inside `Runtime::run_frame()`

Rejected. It violates deterministic frame ownership, latency/failure isolation, replay expectations, and the owner-thread architecture.

### Liquid-owned provider abstraction as the first Stage 2 feature

Rejected for now. Liquid Layer chooses local/hosted inference and direct/agent execution. Add a provider port only if a future reusable Liquid feature truly requires engine-owned inference independent of application policy.

### Hermes as a Liquid dependency

Rejected. Hermes is an external agent/integration target. Approved behaviors must keep working when Hermes is absent, and another agent/runtime must be able to consume the same Liquid semantics.

### MCP as the internal Liquid API

Rejected. MCP is a protocol/transport renderer over the semantic API.

### Full JSON Schema as Liquid's canonical value schema

Rejected for L0. Current `LuaValue` is much smaller, and inference providers/constrained-output systems support different practical schema subsets. Liquid owns a bounded internal vocabulary and renders outward schemas later.

### Trusting provider structured/constrained output

Rejected as an authority mechanism. OpenAI-compatible/self-hosted servers may support JSON schemas, grammars, and tool calls, but feature parity and parser behavior vary. Every model artifact remains locally validated by Liquid/Solid.

### New declarative behavior IR before Lua evidence

Deferred. Lua is already the capability-bounded executable representation built for generated behavior. Add an IR only after real evidence shows recurring problems with authoring, static analysis, reviewability, or portability.

### `SemanticTrigger` / generic adaptive state machine

Deferred. Existing components, Lua watches, named intent replacement/cancellation, intent lifetimes, and application orchestration already solve many scenarios. Introduce a generic abstraction only after several distinct scenarios demonstrate the same missing primitive.

### Continuous notifications whenever an intent loses

Rejected as a default. Losing a resolution is normal Solid behavior. Liquid should make current truth inspectable; Liquid Layer decides which changes justify waking inference.

### Treating agent memory as runtime state

Rejected. Memory may preserve interpretation/history but current behavior/intent/physical truth comes from Solid/Liquid views.

---

# 13. Privacy and Security Principles

These rules apply even before a network transport exists.

- Context is allowlisted/scoped, not dumped wholesale.
- A caller sees only capabilities selected for the current task/scope.
- A readable snapshot does not imply write permission.
- Schema/description metadata never expands actual `World` authority.
- Raw slots, pointers, registry internals, credentials, adapter configuration, unrelated users, and full event history are excluded by default.
- Repair/retry attempts do not receive more authority merely because previous model output failed.
- External agent memory is not trusted as current runtime truth.
- Model/provider constrained output is not trusted validation.
- Trusted descriptions are bounded registration metadata; arbitrary runtime/user strings remain data.
- Remote identity, MCP annotations, tool descriptions, and client claims never replace Solid permission checks.
- Model-facing structures, schemas, snapshots, diagnostics, and later tool results have explicit resource bounds.
- Future adaptive evidence should record exactly which context was shared only under explicit application/privacy policy.

---

# 14. Research Notes and External Precedents

Research was checked against current sources on 28 August 2026. These sources support implementation choices; they do not override repository contracts.

## 14.1 Model Context Protocol

MCP `2026-07-28` introduced a stateless core, header-based routing, cacheable discovery/list results, authorization hardening, an extensions framework, and updated Tier 1 SDKs. Roots, Sampling, and Logging are deprecated for new implementations; direct model-provider integration is the stated replacement for Sampling.

Tool schemas use JSON Schema 2020-12 in the 2026 revision. This does **not** mean Liquid should adopt full JSON Schema internally; it means an MCP renderer can express Liquid's smaller schema without forcing protocol types into the engine.

Security notes relevant to a future remote Liquid server include issuer/audience validation and avoiding token passthrough/confused-deputy designs.

References:

- https://blog.modelcontextprotocol.io/posts/2026-07-28/
- https://blog.modelcontextprotocol.io/posts/2026-07-28-release-candidate/
- https://modelcontextprotocol.io/
- https://ts.sdk.modelcontextprotocol.io/v2/

## 14.2 Home Assistant

Home Assistant is a useful architectural precedent, not a domain/API template. Its integrations register focused semantic LLM APIs with request context, and registered APIs can then be exposed over MCP without making MCP the semantic model itself.

This supports Liquid's direction:

> semantic model-facing API first; transport second; explicit capability scoping instead of administrative/raw API exposure.

Reference:

- https://developers.home-assistant.io/docs/core/llm/

## 14.3 Hermes Agent

Current Hermes documentation reinforces the separation between direct inference and agent execution:

- the full agent loop owns tool use/history/retries/context orchestration;
- plugins may make bounded one-shot `complete` or schema-driven `complete_structured` calls outside the agent loop;
- model/provider routing is configurable independently;
- MCP supports protocol-era negotiation including the `2026-07-28` stateless mode.

This makes Hermes a useful integration target while strengthening the decision not to make it a Liquid dependency.

References:

- https://hermes-agent.nousresearch.com/docs/developer-guide/plugin-llm-access
- https://hermes-agent.nousresearch.com/docs/reference/mcp-config-reference
- https://hermes-agent.nousresearch.com/docs/integrations/providers

## 14.4 Self-hosted / OpenAI-compatible inference

Current llama.cpp and vLLM servers demonstrate that local inference can expose OpenAI-compatible APIs, tool calling, and structured/constrained outputs. This means Liquid Layer can choose local inference without requiring Liquid to own a separate semantic contract.

However, compatibility/structured-output behavior is an integration feature, not a trust guarantee. Liquid/Solid validate every candidate artifact locally.

References:

- https://github.com/ggml-org/llama.cpp/tree/master/tools/server
- https://docs.vllm.ai/en/latest/features/structured_outputs/

---

# 15. Stage 2 Advancement Rule

For every Liquid milestone:

1. start from current `main` on a short-lived branch;
2. state the one missing capability being closed;
3. list the existing Solid facilities reused before adding any abstraction;
4. define public data/header/failure contracts before substantive implementation;
5. write success, stale-state, permission, bounds, and adversarial tests;
6. add no provider/network dependency unless the milestone specifically exists to integrate it;
7. preserve owner-thread confinement and deterministic authority;
8. run strict full tests plus relevant sanitizer/consumer checks;
9. record completion evidence;
10. re-evaluate the next provisional milestone from evidence instead of expanding automatically;
11. update `AGENTS.md`, `DEVELOPMENT_TRACKING.md`, and this plan before widening scope.

A new abstraction should answer a demonstrated requirement that cannot be cleanly solved with existing Solid/Liquid primitives. Folder creation follows accepted milestones, not architectural imagination.

---

# 16. Definition of Stage 2 Success

Liquid is successful when a Liquid Layer application can choose an appropriate model/agent strategy and, through Liquid:

- discover exact bounded authority for behavior authoring;
- generate/submit deterministic Lua behavior proposals;
- validate those proposals against trusted schema, actual Lua capabilities, and real codecs;
- evaluate them through the real Solid execution path;
- inspect current behaviors/intents/resolution/observed truth without registry access or shadow state;
- activate/revise/cancel only through explicit bounded operations;
- correlate adaptive decisions with deterministic runtime evidence;
- replace/remove the external model or agent without invalidating already approved Solid behavior.

The ultimate architecture test remains:

> If every LLM and agent is turned off, already approved behaviors continue to execute, lose and regain intent resolution, expire/cancel according to deterministic policy, dispatch effects, validate feedback, and preserve evidence under Solid alone.
