# Liquid Stage 2 Plan

**Status:** Stage 2 architecture and implementation roadmap  
**Date:** 28 August 2026  
**Foundation:** Solid v0.1.0  
**First implementation milestone:** L0 — Model-Facing Lua Capability Contract

`docs/LIQUID_L0_IMPLEMENTATION_SPEC.md` is the implementation-level companion for L0. This document owns the larger Stage 2 boundary, milestone order, tradeoffs, research, and deferred decisions.

---

## 1. Stage 2 in one sentence

> **Liquid is the reusable model-facing semantic layer that lets external models and agents understand, author, inspect, validate, evaluate, and eventually perform bounded operations over Solid behavior while Solid remains the deterministic execution authority.**

Liquid is not an LLM provider, agent runtime, conversation system, or Liquid Layer policy engine.

---

## 2. Layer boundary

```text
                           LIQUID LAYER
        application policy / sensors / user & environment context
            model/agent selection / invocation / memory
              ┌───────────┼────────────┐
              │           │            │
         local model   hosted call   agent runtime
              │           │            │
              │           │           MCP
              └───────────┴──────┬─────┘
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

### Solid owns

- behavior identity and component access;
- immutable intent ownership/lifetime;
- deterministic resolution;
- frame execution;
- effect commands/reports/observations;
- authoritative external state;
- durable runtime evidence/replay;
- bounded Lua lifecycle execution;
- deterministic simulation.

### Liquid owns

- model-facing capability descriptions;
- deterministic behavior-authoring contracts;
- validation/evaluation surfaces for generated Lua;
- model-friendly runtime inspection composed from Solid truth;
- later bounded operations that preserve Solid ownership/authority;
- later adaptive proposal/approval evidence separate from Solid Event Format v1;
- transport-neutral semantic API later renderable through MCP.

### Liquid Layer owns

- what user/environment facts mean;
- sensors/integrations and application context;
- when inference runs;
- local versus hosted inference;
- direct model call versus agent runtime;
- model/provider selection;
- conversation and user-memory policy;
- neurodivergent-support behavior/policy;
- real smart-home/device integration;
- privacy/consent/user experience.

---

## 3. Frozen Solid invariants

Every Liquid milestone preserves these contracts.

1. `Runtime` remains the sole frame-phase driver.
2. `World` remains the public state/behavior/component lifecycle boundary.
3. Registries, `Coordinator`, `WorldState`, raw slots, component pointers, and mutable internals are not model/remote API.
4. `World` and `Runtime` remain owner-thread confined.
5. Generated executable behavior uses the existing Lua sandbox until evidence proves another representation is needed.
6. Intent semantic contents remain immutable after creation.
7. Current lifetime policies remain `Persistent` and `UntilTime`; explicit cancellation destroys an intent.
8. A live intent that loses resolution remains alive until normal lifetime/cleanup/cancellation removes it.
9. Selected desire remains distinct from command, report, and authoritative observed external state.
10. External state is never optimistically mutated from selection.
11. Slow model/agent work stays outside `Runtime::run_frame()`.
12. Solid Event Format v1 is not casually expanded with prompts, conversations, proposal rationale, or provider metadata.
13. Existing v0.1 source/packaging contracts are not weakened merely for model integration.
14. Model/provider structured-output features are generation aids, never execution authority.

The ultimate test is:

> If every LLM and agent is turned off, already approved behaviors continue executing, losing/regaining resolution, expiring/cancelling deterministically, dispatching effects, validating feedback, and preserving Solid evidence.

---

## 4. Key Solid property Liquid must preserve visibly

```text
intent alive ≠ intent currently selected
```

Example:

```text
FocusSupport owns persistent ON
FollowUser owns OFF
resolver selects FollowUser OFF
```

Then:

```text
FocusSupport ON: alive, not selected
FollowUser OFF: alive, selected
physical light: OFF only after authoritative feedback
```

If FollowUser later stops wanting OFF, the original persistent FocusSupport intent can win again without recreation or a model call.

This means Liquid should expose current truth, not continuously wake an agent just because one of its intents lost a normal conflict.

---

## 5. Lua remains the first generated behavior representation

Solid's Lua boundary already provides the important generated-code safety properties:

- host-fixed behavior owner/time;
- typed named allowlisted capabilities;
- copied snapshots;
- proposal closures instead of direct component mutation;
- fresh VM each frame;
- persistent state through codec-backed components;
- named owner-scoped intent cancellation;
- transactional proposal/cancellation/watch bundles;
- explicit execution/value/memory/source/diagnostic limits;
- no `World`, registries, raw slots/pointers, arbitrary owner, filesystem/network/OS/package/debug/native-code authority.

Do not invent a declarative behavior IR before real generation/evaluation evidence demonstrates a recurring Lua problem.

An approved `LuaBehaviorScript` is also not silently rewritten because context changed. Source modification is an explicit revision operation.

---

## 6. AI execution strategy belongs to the application

Two independent axes exist.

### Direct versus agentic

```text
Direct inference
    bounded context -> one response

Agent execution
    goal -> tools/model/tools/... -> result/action
```

### Local versus hosted

Either reasoning style may use local or hosted inference:

```text
Direct + Local
Direct + Hosted
Agent + Local
Agent + Hosted
```

Liquid Layer chooses among these. Liquid does not.

Hermes Agent is a useful future consumer because it supports a full agent loop, memory/sessions/scheduling/MCP and also bounded direct structured model calls from plugins. That supports, rather than weakens, the separation above.

A model may be stateless between calls. Current runtime truth must still be reconstructible from Liquid/Solid instead of trusted from conversational memory.

---

## 7. Target Liquid semantic surface

These groups define where Stage 2 ends. They do not authorize implementing everything at once.

### Discover

Expose, within a selected scope:

- exact Lua capability paths;
- current permissions;
- exact model-visible read/write shapes;
- bounded trusted meaning/units where useful;
- copied readable values;
- relevant authoring-contract version/limits.

### Author

Accept exact behavior source proposals under a trusted prospective scope without giving the model arbitrary permission selection or direct world mutation.

### Observe

Compose model-friendly current truth from Solid:

- behaviors;
- live owned intents;
- target/value/name/priority/lifetime;
- selected versus not selected;
- selected competitor/owner;
- authoritative observed state;
- bounded causal command/report/evidence context.

Do not maintain a shadow runtime.

### Validate / Evaluate

```text
candidate artifact
  -> declarative schema/capability checks
  -> actual Lua/codec sandbox checks
  -> deterministic Solid simulation scenarios
  -> structured evaluation evidence
```

Simulation proves mechanical/scenario properties, not human suitability.

### Operate

Later expose only bounded ownership-aware operations demonstrated by real scenarios. Never mirror unrestricted `World` mutation or registry APIs.

### Explain

Expose structured causal facts. The model/application may produce language; Liquid does not need a natural-language explanation engine.

### React

Make relevant state/change evidence available, but Liquid Layer chooses which changes should invoke which intelligence.

Do not add a generic `SemanticTrigger`/adaptive state machine until several distinct scenarios prove existing Lua/components/intents/application orchestration are insufficient.

---

## 8. Threading and snapshot direction

`World`/`Runtime` are single-thread-confined. A future HTTP/MCP transport cannot retain a `Runtime&` and call it from arbitrary request threads.

Accepted boundary:

> Owner-thread code produces bounded immutable model-facing copies. Future mutation requests are marshalled back to an owner-controlled path. Transport owns no Runtime authority.

A later runtime view also needs explicit capture consistency. It must not combine "live intents now" with stale resolver selections from an unrelated completed frame and present the mixture as one truth snapshot.

The concrete queue/sidecar/IPC/snapshot publisher is deferred until a milestone actually needs async transport.

---

## 9. Trusted instructions versus dynamic data

Model-facing metadata must distinguish stable trusted contract text from dynamic data.

### Trusted bounded registration metadata

May include:

- binding description;
- field meaning/units;
- stable authoring-contract text/version.

### Dynamic/untrusted data

Includes:

- component values;
- component/integration names;
- sensor/user text;
- event/evidence strings;
- model-generated source/metadata.

Future renderers should encode dynamic material as structured/delimited data instead of interpolating it into trusted instructions.

This does not claim prompt injection can be eliminated; it keeps the Liquid API from creating avoidable instruction/data confusion.

---

# 10. L0 — Model-Facing Lua Capability Contract

**Status:** first implementation milestone.

Detailed implementation semantics and test matrix: `docs/LIQUID_L0_IMPLEMENTATION_SPEC.md`.

## 10.1 Problem closed by L0

A model cannot introspect the executable C++ functions inside `LuaComponentCodec<T>` to learn fields, types, ranges, enums, read/write shape, or semantics.

A snapshot also cannot safely communicate authority.

L0 makes the **existing** prepared behavior's Lua surface machine-readable without invoking any model.

```text
trusted executable Lua binding
     + trusted model metadata
            │
            ▼
current prepared behavior permissions
            │
            ▼
copied readable snapshots
            │
            ▼
immutable bounded capability manifest
```

## 10.2 `LuaValueSchema`

Canonical V1 kinds map to exact `LuaValue` storage:

- Boolean -> `bool`;
- Integer -> `std::int64_t`;
- Number -> finite `double`;
- String -> bounded `std::string`, optional finite enum;
- Array -> `LuaValue::Array`, one item schema/count bounds;
- Object -> `LuaValue::Table`, named required/optional fields, unknown fields rejected by default.

No implicit Integer/Number coercion.

Do not initially add null, bytes, unions/composition, `$ref`, recursive schema graphs, regex, arbitrary JSON Schema, or provider-specific schema keywords without a real current codec requirement.

Liquid's schema remains a small internal type. MCP/provider schemas are future renderings.

## 10.3 Separate read and write schemas

`LuaComponentCodec<T>` has independent directions:

```text
encode(Component) -> LuaValue
decode(LuaValue) -> Component
```

Solid does not require symmetry. Therefore model-visible metadata distinguishes:

```text
readSchema  = shape produced by encode
writeSchema = shape accepted by decode
```

Most simple codecs can register one symmetric shape through a helper, but the contract must support asymmetric codecs from the start.

A read-only sensor also needs `readSchema`; raw value alone is not sufficient for reliable model interpretation.

## 10.4 Manifest projection

For a fully described binding:

| `World` permission | manifest read side | manifest write side |
| --- | --- | --- |
| Read | `readSchema` + copied value | absent |
| Write | absent | `writeSchema` |
| ReadWrite | `readSchema` + copied value | `writeSchema` |

Permission remains sourced from the real current `World` access table.

Existing schema-less `expose_component(...)` remains source-compatible/executable for human/trusted scripts but is absent from model discovery.

## 10.5 Build through `LuaBehaviorRunner`

Do not create another capability registry.

The runner already owns the Lua bindings, script names, codec callbacks, current permission-description machinery, and host snapshots. Model metadata belongs beside those bindings and freezes with them before execution.

The manifest is an immutable copied product of that existing boundary.

## 10.6 Exact authoring details matter

The model contract must reflect the real Lua language boundary:

- Integer and Number are distinct;
- host-provided empty arrays preserve a private Array marker;
- literal `{}` is an empty Object, so a write-only empty Array currently has no magic construction syntax;
- host generates exact access expressions, including safe bracket/quoted forms for unusual names;
- a dedicated authoring-contract version is distinct from behavior source revision and Solid component schema version.

## 10.7 L0 validation layering

```text
readSchema
  validates what model is told it can read

writeSchema
  validates documented proposal shape

actual Lua codec decode
  validates trusted executable proposal semantics

World permission + host-bound closure
  validates current authority

intent transaction
  validates/commits current runtime operation atomically
```

No schema/provider feature replaces lower authority.

## 10.8 L0 files

Expected area only:

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
docs/LIFECYCLE_SCRIPTING.md    # only if executable public contract changes
```

No `adaptive/`, provider, agent, MCP directory, new exported target, or dependency in L0.

## 10.9 L0 evidence gates

The detailed spec is normative, but the milestone must at least prove:

- every exact schema kind and bound;
- Integer/Number non-coercion;
- symmetric and intentionally asymmetric codec fixtures;
- actual readable snapshots validated by `readSchema`;
- representative write-schema values exercised through actual decoder;
- exact permission projection;
- access revoke/remove rebuild behavior;
- host-generated unusual-name access expressions work in actual sandbox;
- immutable copied manifest data/no internals;
- bounded descriptions/schema/manifest/diagnostics;
- current empty-array semantics not misrepresented;
- old schema-less Lua exposure remains source-compatible;
- full strict regression stays green.

## 10.10 Explicit L0 non-goals

No:

- model/provider call;
- HTTP/API key/routing/fallback;
- Hermes integration;
- MCP dependency;
- prompt/repair loop;
- behavior proposal persistence/activation;
- generic runtime inspection;
- remote mutation;
- new behavior DSL/IR;
- adaptive trigger abstraction;
- hardware/MQTT/voice/biosignals;
- Liquid Layer policy;
- Solid Event Format v1 changes.

---

# 11. Provisional roadmap after L0

Only L0 is implementation-authorized by this roadmap. Re-evaluate each next stage from evidence; do not automatically execute L1-L6.

## L1 — Read-Only Liquid Runtime View

**Question:** Can a caller reconstruct relevant current Solid truth without registry access, shadow state, or conversation memory?

Likely scope:

- owner-thread immutable snapshots;
- API/model-safe behavior references;
- live owned intents with names/values/targets/priorities/lifetimes;
- selected/not-selected and selected competitor/owner;
- authoritative observed state;
- bounded command/report/evidence facts;
- defined capture/frame consistency.

Known gaps to solve deliberately:

- stable type/component naming from a generic `ComponentTarget` without exposing `ComponentRegistry`;
- mapping external component targets to observed state without exposing private Runtime effect bindings;
- avoiding a mixed snapshot of live current intents and stale resolver selection;
- avoiding presentation of world-local generational handles as permanent historical identifiers.

Acceptance scenario: FocusSupport persistent `ON` loses to FollowUser `OFF`, remains alive, and becomes selectable again after the competitor disappears. One captured view must represent the truth correctly.

Do not add MCP just to make L1 look integrated.

## L2 — Behavior Proposal and Prospective Authoring Scope

**Question:** Can an external model propose a new behavior without receiving a broadly privileged live behavior merely for discovery?

Likely scope:

- exact immutable Lua-source proposal;
- bounded review metadata;
- trusted prospective scope selected by host/application;
- manifest for that scope;
- contract/source/capability validation;
- stale-scope detection;
- explicit new behavior versus approved-source revision distinction;
- no model-selected arbitrary permissions.

L2 decides how prospective scope relates to Solid behavior identity. Possible approaches include a prepared non-executing behavior or a separate host-owned scope representation. Do not decide before L0/L1 evidence.

## L3 — Deterministic Proposal Evaluation

**Question:** Can a candidate behavior run through the real Solid execution path before activation?

Likely scope:

- isolated prepared `World`/`Runtime`;
- real `LuaLifecycleSystem`/runner;
- real intents/lifetime/resolution;
- real `InMemoryAdapter`;
- deterministic scenarios;
- report separating proposals, selections, commands, reports, observations, final authoritative state;
- bounded errors suitable for an external repair loop.

Current `SimulationScenario` is light-specific. Generalize only what proposal-evaluation tests require.

## L4 — MCP Adapter

**Question:** Can external agent runtimes consume the same semantic API without gaining authority?

Likely first tool surface:

- focused capability/discovery;
- read-only runtime inspection;
- behavior validation/evaluation;
- no unrestricted mutators.

Current protocol direction (28 August 2026):

- target stable MCP revision at implementation time; current stable is `2026-07-28`;
- 2026 core is stateless;
- do not build new features on deprecated Sampling/Roots/Logging;
- model invocation remains client/application-owned rather than server Sampling;
- MCP input/output schemas render Liquid's own contracts;
- keep tools few, focused, context-scoped;
- tool descriptions/annotations are hints, not authority;
- validate requests/results locally;
- preserve owner-thread confinement through copied snapshots and marshalled mutation requests;
- prefer local/owner-controlled deployment before remote OAuth complexity unless a real requirement exists;
- if remote auth is needed, validate intended issuer/audience and never pass inbound client tokens through to downstream services.

SDK/language is deliberately deferred. Current official MCP Tier 1 SDKs are TypeScript, Python, Go, and C#; no official C++ SDK is currently listed.

Hermes is an integration proof, not contract owner. Current Hermes documentation supports protocol-era negotiation including the 2026 stateless probe. Test a second client/conformance path too.

## L5 — Approval, Activation, Revision, and Bounded Operations

**Question:** Can reviewed generated behavior become live/revised/stopped without stale approval, source substitution, or authority expansion?

Likely scope:

- approval bound to exact source/proposal hash;
- scope/access revision and evaluation evidence binding;
- installation of exact approved source revision;
- explicit revision workflow;
- ownership-aware stop/cancel/remove operations justified by scenarios;
- no arbitrary world/intent mutation.

Critical revision issue:

> What happens to persistent intents created by the previous approved script revision?

Changing `LuaBehaviorScript.source/revision` resets lifecycle execution state but does not itself define the semantic fate of already-live persistent intents. L5 must explicitly solve this before claiming safe revision semantics.

## L6 — Liquid Proposal Evidence and Change Surface

**Question:** Can adaptive decisions be audited and applications receive bounded changes without contaminating Solid evidence?

Likely scope:

- separate proposal/evaluation/approval/revision records;
- correlation to Solid session/behavior/runtime evidence;
- optional provider/model metadata supplied by the application that performed inference;
- privacy-aware record of context/capabilities shared;
- bounded change cursor/feed only if Liquid Layer scenarios prove it necessary.

Liquid evidence answers why an adaptive artifact changed. Solid evidence continues to answer what deterministic execution did/observed.

---

## 12. Deferred/rejected directions

### LLM inside `Runtime::run_frame()`
Rejected: latency, determinism, failure isolation, replay, owner-thread boundary.

### Liquid-owned provider abstraction now
Rejected: model routing belongs to Liquid Layer unless a future reusable Liquid feature proves engine-owned inference necessary.

### Hermes as a dependency
Rejected: external optional agent consumer.

### MCP as internal Liquid API
Rejected: protocol/transport only.

### Full JSON Schema as canonical schema
Rejected for L0: larger than current `LuaValue`; provider practical subsets vary; render outward later.

### One schema assumed for both Lua codec directions
Rejected: `encode` and `decode` are independent and may be asymmetric. L0 models read/write shape explicitly.

### Provider structured output as trust boundary
Rejected: always validate locally.

### New behavior IR before Lua evidence
Deferred.

### Generic semantic trigger/adaptive state machine
Deferred until repeated scenarios prove a missing engine primitive.

### Continuous notification when an intent loses
Rejected as default: normal resolver behavior; application decides significance.

### Agent memory as runtime state
Rejected: current truth comes from Solid/Liquid views.

---

## 13. Privacy/security rules across Stage 2

- Context is allowlisted/scoped, not dumped wholesale.
- Readable data does not imply write authority.
- Read/write schema metadata does not enlarge `World` permission.
- Raw slots, pointers, registries, credentials, adapter config, unrelated users, and full history are excluded by default.
- Repair/retry never gains authority because earlier output failed.
- Agent memory is not current runtime truth.
- Provider structured/constrained output is not host validation.
- Trusted descriptions are bounded registration metadata; dynamic strings remain data.
- Remote identity/MCP annotations never replace Solid permission checks.
- Model-facing values/schemas/manifests/diagnostics/tool results are explicitly bounded.
- Future adaptive evidence records shared context only under explicit privacy policy.

---

## 14. External research checked on 28 August 2026

These references support design choices but never override repository contracts.

### MCP

MCP `2026-07-28` introduced a stateless core, header routing, cacheable list/discovery results, authorization hardening, extensions, and updated Tier 1 SDKs. Sampling/Roots/Logging are deprecated for new implementations; direct provider integration is the stated replacement for Sampling. Tool input/output schemas can use JSON Schema 2020-12.

- https://blog.modelcontextprotocol.io/posts/2026-07-28/
- https://blog.modelcontextprotocol.io/posts/2026-07-28-release-candidate/
- https://ts.sdk.modelcontextprotocol.io/v2/
- https://modelcontextprotocol.io/

### Home Assistant

Home Assistant provides a useful architectural precedent: focused semantic LLM APIs are registered independently and can then be served over MCP. The lesson is semantic API first, transport second—not to copy Home Assistant's domain API.

- https://developers.home-assistant.io/docs/core/llm/

### Hermes Agent

Current Hermes docs distinguish the full agent loop from bounded direct plugin LLM calls and document MCP protocol-era negotiation including 2026 stateless mode. Hermes also supports self-hosted/provider routing.

- https://hermes-agent.nousresearch.com/docs/developer-guide/plugin-llm-access
- https://hermes-agent.nousresearch.com/docs/reference/mcp-config-reference
- https://hermes-agent.nousresearch.com/docs/integrations/providers

### Hosted/self-hosted structured inference

Current OpenAI APIs, llama.cpp, and vLLM support structured/constrained output/tool-calling in different forms. Practical schema support/compatibility is not identical, reinforcing the decision to keep Liquid's canonical contract small and validate locally.

- https://developers.openai.com/
- https://github.com/ggml-org/llama.cpp/tree/master/tools/server
- https://docs.vllm.ai/en/latest/features/structured_outputs/

---

## 15. Milestone advancement rule

For every Liquid milestone:

1. start from current `main` on a short-lived branch;
2. state one missing capability being closed;
3. identify existing Solid facilities reused before adding abstractions;
4. define public data/header/failure semantics before substantive implementation;
5. write success/stale/permission/bounds/adversarial tests;
6. add no provider/network dependency unless that milestone specifically integrates it;
7. preserve owner-thread/deterministic authority;
8. run strict full regression plus relevant sanitizer/consumer checks;
9. record completion evidence;
10. re-evaluate the next provisional milestone rather than expanding automatically;
11. update `AGENTS.md`, `DEVELOPMENT_TRACKING.md`, and Stage 2 docs before widening scope.

A new abstraction must solve a demonstrated requirement current Solid/Liquid primitives cannot cleanly solve. Folder creation follows accepted milestones, not architectural imagination.

---

## 16. Definition of Stage 2 success

Liquid is successful when a Liquid Layer application can choose its own model/agent strategy and, through Liquid:

- discover exact bounded behavior-authoring capabilities;
- create/submit deterministic Lua behavior proposals;
- validate read/write shapes against trusted metadata and actual codec/World authority;
- evaluate candidates through the real Solid path;
- inspect current behaviors/intents/resolution/observed truth without registry access or shadow state;
- activate/revise/cancel only through explicit bounded operations;
- correlate adaptive decisions with deterministic runtime evidence;
- replace/remove the external model/agent without invalidating approved Solid behaviors.
