# Liquid Stage 2 Plan

Related research: [Semantic invariance in Liquid and MCP](SEMANTIC_INVARIANCE_RESEARCH.md)
proposes evaluating paraphrases against verified outcomes and authority, with
meaning-changing controls. It does not activate or expand an implementation milestone.

**Status:** Stage 2 architecture and implementation roadmap  
**Date:** 28 August 2026  
**Foundation:** Solid v0.1.0  
**First implementation milestone:** L0 — Model-Facing Lua Capability Contract

`docs/LIQUID_L0_IMPLEMENTATION_SPEC.md` is the implementation companion for L0. This document owns the larger Stage 2 boundary, milestone order, tradeoffs, research, and deferred decisions.

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

- behavior identity/component access;
- immutable intent ownership/lifetime;
- deterministic resolution/frame execution;
- effect commands/reports/observations;
- authoritative external state;
- durable runtime evidence/replay;
- bounded Lua lifecycle execution;
- deterministic simulation.

### Liquid owns

- model-facing capability descriptions;
- deterministic behavior-authoring contracts;
- generated Lua proposal/validation/evaluation surfaces;
- model-friendly runtime inspection composed from Solid truth;
- later bounded operations preserving Solid authority;
- later adaptive proposal/approval evidence separate from Solid Event Format v1;
- transport-neutral semantics later renderable through MCP.

### Liquid Layer owns

- user/environment meaning;
- sensors/integrations and application context;
- when inference runs;
- local/hosted selection;
- direct call/agent selection;
- model/provider selection;
- conversation/user memory;
- neurodivergent-support policy;
- real smart-home/device integration;
- privacy/consent/user experience.

---

## 3. Frozen Solid invariants

Every Liquid milestone preserves:

1. `Runtime` remains the sole frame-phase driver.
2. `World` remains the public state/behavior/component lifecycle boundary.
3. Registries, `Coordinator`, `WorldState`, raw slots/pointers, and mutable internals are not model/remote API.
4. `World` and `Runtime` remain owner-thread confined.
5. Generated executable behavior uses the existing Lua sandbox until evidence proves another representation is needed.
6. Intent semantic contents remain immutable after creation.
7. Current lifetimes remain `Persistent` and `UntilTime`; explicit cancellation destroys an intent.
8. A live losing intent remains live until normal expiration/cleanup/cancellation.
9. Selected desire remains distinct from command, report, and authoritative observed state.
10. External state is never optimistically mutated from selection.
11. Slow model/agent work stays outside `Runtime::run_frame()`.
12. Solid Event Format v1 is not casually expanded with prompts/conversations/proposal rationale/provider metadata.
13. Existing v0.1 source/package contracts are not weakened merely for model integration.
14. Provider structured/constrained output is generation assistance, never execution authority.

Ultimate test:

> If every LLM/agent is disabled, already approved behaviors keep executing, losing/regaining resolution, expiring/cancelling deterministically, dispatching effects, validating feedback, and preserving Solid evidence.

---

## 4. Intent survival is a first-class fact

```text
intent alive ≠ intent currently selected
```

Example:

```text
FocusSupport persistent ON: alive, not selected
FollowUser OFF:             alive, selected
physical light:             OFF only after authoritative feedback
```

When FollowUser stops wanting OFF, the original FocusSupport intent may win again without recreation/model involvement.

Liquid should expose this truth when asked. It should not wake an agent for every normal resolver loss by default.

---

## 5. Lua remains the first generated behavior representation

The current Lua boundary already provides:

- host-fixed owner/time;
- typed named allowlisted capabilities;
- copied snapshots;
- proposal closures rather than mutation authority;
- fresh VM per frame;
- persistent state through codec-backed components;
- named owner-scoped cancellation;
- transactional proposal/cancellation/watch bundles;
- execution/value/memory/source/diagnostic bounds;
- no `World`, registry, raw slot/pointer, arbitrary owner, OS/I/O/package/debug/native authority.

Do not introduce a new behavior IR until actual generation/evaluation evidence demonstrates a repeated Lua problem.

An approved `LuaBehaviorScript` is not silently rewritten because context changed. Source modification is an explicit revision.

---

## 6. AI execution strategy belongs to Liquid Layer

Two independent axes exist:

```text
Direct inference  vs  Agent execution
Local model       vs  Hosted model
```

Valid combinations include Direct+Local, Direct+Hosted, Agent+Local, Agent+Hosted.

Hermes is a useful future consumer because it supports a full agent loop plus bounded direct structured plugin calls. It remains optional and replaceable.

A model can be stateless between calls; current runtime truth must remain reconstructible from Liquid/Solid rather than trusted from conversation memory.

---

## 7. Target Liquid semantic surface

### Discover

Within selected scope expose exact capability paths, current permission, read/write value shapes, bounded trusted semantics, copied readable values, and authoring-contract version/limits.

### Author

Accept exact Lua proposals under host/application-selected prospective authority. The model does not choose arbitrary permissions.

### Validate / Evaluate

```text
candidate
  -> schema/capability checks
  -> actual Lua/codec sandbox validation
  -> deterministic Solid scenarios
  -> structured evidence
```

Simulation demonstrates mechanics/scenario behavior, not human suitability.

### Observe

Compose current model-friendly truth from Solid without shadow state: behaviors, live intents, selection conflicts, observed state, and bounded causal evidence.

### Operate

Later expose only scenario-justified ownership-aware operations; never raw generic `World`/registry mutation.

### Explain

Expose structured causal facts. External models/applications generate natural language.

### React

Expose bounded state/change information; Liquid Layer decides whether/which intelligence runs. No generic semantic-trigger engine until repeated scenarios demonstrate a missing primitive.

---

## 8. Threading / snapshot direction

Future HTTP/MCP code cannot retain `Runtime&` and call it from arbitrary request threads.

> Owner-thread code produces bounded immutable model-facing copies. Mutation requests are later marshalled to an owner-controlled path. Transport owns no Runtime authority.

A future runtime view must also define one consistency capture point instead of combining live intents from one moment with stale selections from another.

Concrete queue/sidecar/IPC machinery is deferred until needed.

---

## 9. Trusted metadata versus dynamic data

Trusted bounded registration metadata may contain binding/field descriptions, units, and stable authoring-contract text/version.

Dynamic data includes component values/names, sensor/user text, evidence strings, and model output. Future renderers carry these as structured/delimited data rather than interpolating them as trusted instructions.

---

# 10. L0 — Model-Facing Lua Capability Contract

**Status:** first implementation milestone.  
**Detailed semantics:** `docs/LIQUID_L0_IMPLEMENTATION_SPEC.md`.

### Problem

A model cannot introspect executable `LuaComponentCodec<T>` callbacks to discover shape/range/semantics. Snapshots also do not communicate authority.

### Solution

```text
trusted Lua binding + trusted model metadata
                │
                ▼
prepared behavior current permissions
                │
                ▼
copied readable snapshots
                │
                ▼
immutable bounded capability manifest
```

No model call occurs in L0.

### Canonical schema

`LuaValueSchema` exactly models current `LuaValue` storage:

- Boolean -> `bool`;
- Integer -> `std::int64_t`;
- Number -> finite `double`;
- String -> bounded string/optional enum;
- Array -> one child schema/count bounds;
- Object -> required/optional named fields, unknown fields rejected by default.

No Integer/Number coercion. No null/bytes/unions/`$ref`/regex/full JSON Schema/provider keywords without a real current codec requirement.

### Read/write directions

`LuaComponentCodec<T>` has independent executable directions:

```text
encode(Component) -> LuaValue  => readSchema
decode(LuaValue) -> Component  => writeSchema
```

Do not assume symmetry. Provide a symmetric helper for ordinary codecs, but test an asymmetric codec.

For a fully described binding:

| `World` permission | manifest read side | manifest write side |
| --- | --- | --- |
| Read | `readSchema` + copied value | absent |
| Write | absent | `writeSchema` |
| ReadWrite | `readSchema` + copied value | `writeSchema` |

Existing schema-less Lua exposure stays source-compatible/executable but is not model-discoverable.

### Single source of capability truth

Build the manifest through `LuaBehaviorRunner`; do not create another binding registry. Model metadata lives beside the executable binding and freezes with it before execution.

### Real Lua authoring constraints remain visible

- Integer and Number are distinct.
- Host-provided empty arrays preserve the private Array marker; literal `{}` is empty Object.
- A write-only empty Array has no hidden magic construction syntax today.
- Host generates exact access expressions for unusual names.
- Authoring-contract version is separate from behavior revision and Solid component schema version.

### Validation layers

```text
readSchema -> documented/encoded read value
writeSchema -> documented proposal value
codec decode -> executable host write validation
World/closure -> current authority
transaction -> actual atomic commit
```

No upper layer replaces a lower authority layer.

### L0 implementation area

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
relevant Stage 2/Lua documentation
```

L0 remains in `Liquid::Lua`. No provider/MCP/agent dependency or new exported target.

### L0 gates

- exact schema kinds/bounds/non-coercion;
- symmetric + asymmetric codec fixtures;
- readable snapshots validate against `readSchema`;
- write fixtures exercised through actual decoder;
- exact permission projection;
- access revoke/remove rebuild behavior;
- deterministic real-sandbox access-path tests;
- immutable copied bounded manifest;
- current empty-array limitation represented accurately;
- legacy schema-less exposure remains compatible;
- full strict regression green.

---

# 11. Provisional roadmap after L0

Only L0 is implementation-authorized by this roadmap. Re-evaluate every later milestone from prior evidence.

## L1 — Behavior Proposal and Prospective Authoring Scope

**Dependency:** L0.  
**Question:** Can an external author propose a new behavior without granting a broadly privileged live behavior merely to discover capabilities?

Likely scope:

- immutable proposal containing exact Lua source and bounded review metadata;
- trusted prospective scope selected by host/application;
- L0-style capability manifest for that scope;
- contract/source/capability validation;
- stale-scope detection;
- explicit new behavior versus approved-behavior revision distinction;
- no model-selected arbitrary permission.

Key design choice deferred to L1: represent prospective scope using a prepared non-executing Solid behavior or a separate host-owned scope object. Choose from L0 evidence; do not speculate now.

Dynamic user/environment strings remain structured data separate from stable authoring instructions.

## L2 — Deterministic Proposal Evaluation

**Dependency:** L1 proposal artifact/scope.  
**Question:** Can a candidate behavior be evaluated through the real Solid path before activation?

Likely scope:

- isolated prepared `World`/`Runtime`;
- real `LuaLifecycleSystem`/runner;
- real intent lifetime/resolution;
- real `InMemoryAdapter`;
- deterministic scenario inputs;
- evidence separating proposals, selections, commands, reports, observations, final authoritative state;
- bounded failures suitable for external repair.

Current `SimulationScenario` is light-specific. Generalize only what L2 tests demand.

This closes the first complete authoring/evaluation vertical slice without requiring any model provider.

## L3 — Read-Only Liquid Runtime View

**Dependency:** no strict dependency on L2, but scheduled here so the first vertical slice reaches evaluation before generic observability work.  
**Question:** Can a caller reconstruct current relevant Solid truth without registries, shadow state, or conversational memory?

Likely scope:

- owner-thread immutable snapshots;
- API/model-safe behavior references;
- live owned intents (name/value/target/priority/lifetime);
- selected/not-selected + competitor/owner;
- authoritative observed state;
- bounded command/report/evidence facts;
- explicit snapshot/frame consistency.

Known gaps:

- generic stable type/component naming from `ComponentTarget` without registry exposure;
- external target -> observed-state mapping without exposing private Runtime bindings;
- consistent capture of live intents + resolver selection;
- no misuse of world-local generational handles as permanent IDs.

Acceptance: FocusSupport persistent ON loses to FollowUser OFF, remains live, and can win later; one captured view represents all facts correctly.

## L4 — MCP Adapter

**Dependency:** stable semantic capabilities from L0-L3.  
**Question:** Can external agent runtimes use the same Liquid API without gaining authority?

Likely initial tools:

- discovery/capability;
- proposal validation/evaluation;
- read-only runtime inspection;
- no unrestricted mutators.

Current direction (28 Aug 2026):

- current stable MCP is `2026-07-28`, with stateless core;
- no new work on deprecated Sampling/Roots/Logging;
- model invocation remains client/application-owned;
- MCP schemas render Liquid types rather than become canonical engine types;
- tools remain few/focused/context-scoped;
- tool descriptions/annotations are not authorization;
- server validates request/result locally;
- owner-thread confinement preserved via copied snapshots/marshalled requests;
- local/owner-controlled deployment first unless remote auth is actually required;
- if remote auth arrives, validate issuer/audience and never pass inbound client tokens through to downstream services.

SDK/language deferred: current official Tier 1 SDKs are TypeScript, Python, Go, C#; no official C++ SDK is listed.

Hermes is one integration proof, not contract owner. Current Hermes docs support 2026 protocol negotiation; also test another client/conformance path.

## L5 — Approval, Activation, Revision, and Bounded Operations

**Dependency:** proposal/evaluation semantics + inspection surface.  
**Question:** Can reviewed behavior become live/revised/stopped without stale approval, source substitution, or authority expansion?

Likely scope:

- approval bound to exact source/proposal hash;
- scope/access revision and evaluation evidence binding;
- exact approved source installation;
- explicit revision workflow;
- ownership-aware stop/cancel/remove operations justified by scenarios;
- no arbitrary world/intent mutator.

Critical question:

> What happens to persistent intents created by the previous approved script revision?

Changing `LuaBehaviorScript.source/revision` does not itself define the fate of already-live persistent intents. L5 must.

## L6 — Liquid Proposal Evidence and Change Surface

**Dependency:** actual proposal/approval lifecycle.  
**Question:** Can adaptive decisions be audited and applications consume bounded changes without contaminating Solid evidence?

Likely scope:

- separate proposal/evaluation/approval/revision records;
- correlation to Solid session/behavior/runtime evidence;
- optional provider/model metadata supplied by the application that actually inferred;
- privacy-aware record of context/capabilities shared;
- bounded change cursor/feed only if Liquid Layer proves it necessary.

Liquid evidence answers **why adaptive artifacts changed**. Solid evidence continues answering **what deterministic execution did/observed**.

---

## 12. Deferred/rejected directions

- **LLM inside `Runtime::run_frame()`** — rejected.
- **Liquid-owned provider abstraction now** — rejected; routing belongs to Liquid Layer unless engine-owned inference is later demonstrated.
- **Hermes dependency** — rejected; external optional consumer.
- **MCP as internal API** — rejected; transport only.
- **Full JSON Schema as canonical schema** — rejected for L0; render outward later.
- **One schema assumed for codec read/write** — rejected; `encode`/`decode` may be asymmetric.
- **Provider structured output as trust boundary** — rejected; always validate locally.
- **New behavior IR before Lua evidence** — deferred.
- **Generic semantic-trigger state machine** — deferred until repeated scenarios prove a missing primitive.
- **Continuous notifications on intent loss** — rejected as default; application decides significance.
- **Agent memory as runtime state** — rejected; current truth comes from Solid/Liquid.

---

## 13. Privacy/security rules

- Scope/context is allowlisted, not wholesale.
- Read data does not imply write authority.
- Read/write schema metadata does not enlarge `World` permission.
- Raw slots/pointers/registries/credentials/adapter config/unrelated users/full history are excluded by default.
- Repair retries do not gain authority after failure.
- Agent memory is not current runtime truth.
- Provider constrained output is not host validation.
- Trusted descriptions are bounded registration metadata; dynamic strings stay data.
- MCP identity/annotations never replace Solid permission.
- Values/schemas/manifests/diagnostics/tool results are bounded.
- Future adaptive evidence records shared context only under explicit privacy policy.

---

## 14. External research checked on 28 August 2026

### MCP

`2026-07-28` introduced a stateless core, header routing, cacheable discovery/list results, authorization hardening, extensions, and updated Tier 1 SDKs. Sampling/Roots/Logging are deprecated for new implementations; direct provider integration replaces Sampling. Tool schemas support JSON Schema 2020-12.

- https://blog.modelcontextprotocol.io/posts/2026-07-28/
- https://blog.modelcontextprotocol.io/posts/2026-07-28-release-candidate/
- https://ts.sdk.modelcontextprotocol.io/v2/
- https://modelcontextprotocol.io/

### Home Assistant

Focused semantic LLM APIs are registered independently and can then be exposed over MCP. Architectural lesson: semantic API first, transport second.

- https://developers.home-assistant.io/docs/core/llm/

### Hermes Agent

Current docs distinguish full agent execution from bounded direct plugin LLM calls and support MCP protocol-era negotiation including 2026 stateless mode/self-hosted providers.

- https://hermes-agent.nousresearch.com/docs/developer-guide/plugin-llm-access
- https://hermes-agent.nousresearch.com/docs/reference/mcp-config-reference
- https://hermes-agent.nousresearch.com/docs/integrations/providers

### Structured/self-hosted inference

OpenAI APIs, llama.cpp, and vLLM provide structured/constrained outputs/tool-calling in different forms and practical schema subsets. Liquid therefore keeps its canonical contract provider-neutral and validates locally.

- https://developers.openai.com/
- https://github.com/ggml-org/llama.cpp/tree/master/tools/server
- https://docs.vllm.ai/en/latest/features/structured_outputs/

---

## 15. Advancement rule

For each milestone:

1. start a short-lived branch from current `main`;
2. close one missing capability;
3. identify reused Solid primitives before new abstractions;
4. define data/header/failure semantics first;
5. write success/stale/permission/bounds/adversarial tests;
6. add no provider/network dependency unless that milestone integrates it;
7. preserve owner-thread/deterministic authority;
8. run strict regression + relevant sanitizer/consumer checks;
9. record completion evidence;
10. re-evaluate the next provisional milestone rather than auto-expanding;
11. update operational/tracking/design docs before widening scope.

A new abstraction must solve a demonstrated requirement current primitives cannot cleanly solve.

---

## 16. Definition of Stage 2 success

A Liquid Layer application can choose any suitable model/agent strategy and through Liquid:

- discover exact bounded authoring capabilities;
- create/submit Lua behavior proposals;
- validate read/write shape plus actual codec/World authority;
- evaluate candidates through real Solid execution;
- inspect behaviors/intents/resolution/observed truth without registry/shadow state;
- activate/revise/cancel only through explicit bounded operations;
- correlate adaptive decisions with deterministic evidence;
- replace/remove its external model/agent without invalidating approved Solid behavior.
