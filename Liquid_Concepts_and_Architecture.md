# Liquid Concepts and Architecture v0.4

**Status:** Accepted Stage 2 architecture baseline  
**Date:** 28 August 2026  
**Foundation:** Solid v0.1.0  
**Current implementation milestone:** L0 — Model-Facing Lua Capability Contract

---

## 1. Purpose

This document defines the conceptual architecture and vocabulary of Liquid after completion of Solid v0.1.0 and the Stage 2 design discussion.

It answers three questions:

1. what Solid already owns and must continue to own;
2. what Liquid adds on top of Solid;
3. what remains application policy in Liquid Layer.

Implementation order, milestone evidence, rejected alternatives, and research are in `docs/LIQUID_STAGE2_PLAN.md`. L0 implementation-level semantics are in `docs/LIQUID_L0_IMPLEMENTATION_SPEC.md`. Operational coding-agent scope is in `AGENTS.md`.

---

## 2. Project Layers

### Solid

Solid is the completed deterministic execution foundation.

It owns runtime truth: behaviors, component access, immutable intents, intent lifetime, deterministic conflict resolution, frame execution, effects, feedback, authoritative observations, durable evidence/replay, bounded Lua lifecycle execution, and simulation.

Solid v0.1.0 is complete through M1-M6 and finalization S0-S7.

### Liquid

Liquid is the reusable **model-facing control and authoring layer over Solid**.

Its purpose is to make Solid safely understandable and operable by external models/agent runtimes without making those models part of deterministic Runtime execution.

Liquid provides the semantic surface needed to:

- discover bounded behavior capabilities;
- author deterministic Lua behavior proposals;
- validate and evaluate proposals;
- inspect current runtime truth;
- eventually perform explicitly bounded operations;
- expose structured causal evidence;
- later render the same surface through transports such as MCP.

Liquid does **not** choose a model/provider, own a conversation loop, decide when inference occurs, or define neurodivergent-support policy.

### Liquid Layer

Liquid Layer is the final adaptive smart-environment application/research system.

It owns meaning and application policy:

- user goals/preferences;
- sensors/environment context;
- when a model or agent should be invoked;
- local versus hosted inference;
- direct model call versus agent runtime;
- conversation/user-memory policy;
- neurodivergent-support scenarios;
- real smart-home/device integration;
- consent/privacy/user-facing control.

Liquid Layer uses Liquid in the same way Liquid uses Solid: as a reusable lower-level tool with explicit boundaries.

---

## 3. Architectural Thesis

> Liquid lets probabilistic systems understand, synthesize, inspect, and negotiate behavior while Solid remains the deterministic authority that executes, constrains, resolves, audits, and reproduces it.

```text
                           LIQUID LAYER
          application policy / user & environment context
           AI routing / sensors / memory / conversation
              ┌───────────┼────────────┐
              │           │            │
         local model   hosted call   agent runtime
              │           │            │
              │           │           MCP
              └───────────┴──────┬─────┘
                                 ▼
                              LIQUID
                model-facing semantic capabilities
                  discover / author / observe
                  validate / evaluate / operate
                                 │
                                 ▼
                               SOLID
                deterministic execution and evidence
```

A generated/approved behavior must continue to execute correctly if every LLM and agent is unavailable.

---

## 4. Solid Authority Is Not Duplicated

Liquid composes model-facing views from Solid's existing sources of truth. It does not maintain a second runtime or competing shadow state.

Fundamental distinctions:

```text
live intent / desire
    ≠ selected intent
    ≠ dispatched command
    ≠ adapter report
    ≠ authoritative observed state
```

A selected desire does not prove an external device changed. Externally controlled component state changes only after correlated/validated authoritative feedback or a revisioned external observation.

Likewise:

```text
intent alive
    ≠ intent currently selected
```

A persistent intent that loses a conflict remains alive. If the winning competitor disappears, the older live intent may become selected again without recreation or model involvement.

This property reduces the amount of adaptive supervision Liquid needs: models do not continuously reconstruct preferences Solid already preserves.

---

## 5. Behaviors, Lua, and Intents

### Behavior

A behavior is Solid's main runtime identity and owns intents created on its behalf.

Liquid may create, inspect, evaluate, revise, or later operate behaviors through approved boundaries, but does not replace Solid's behavior identity model.

Do not assume an external adaptive agent must itself be a Solid behavior or carry an `Agent` component. An agent runtime may remain entirely outside Solid and consume Liquid.

### Lua behavior source

`LuaBehaviorScript` remains the first generated executable behavior representation.

This is deliberate. Solid's Lua boundary already provides:

- host-fixed owner;
- host-fixed monotonic time;
- fresh VM per behavior/frame;
- persistent state through ordinary codec-backed components;
- copied snapshots;
- typed/named/allowlisted access;
- proposal closures instead of mutation authority;
- transactional named proposals/cancellations/watches;
- bounded instructions/memory/source/values/tables/strings/diagnostics/intents/cancellations/watches;
- no `World`, registry, storage, raw slot/pointer, arbitrary owner, OS/I/O/package/debug, or arbitrary native execution authority.

The executable contract is normative in `docs/LIFECYCLE_SCRIPTING.md` and current scripting headers.

A separate behavior IR is deferred until real model-generation/evaluation evidence demonstrates a recurring problem Lua cannot solve cleanly.

### Intent

An intent is an immutable proposed state/effect owned by a behavior.

Current lifetime policies:

- `Persistent`;
- `UntilTime` using monotonic session-relative `IntentTime`.

Explicit cancellation destroys the intent.

Intent lifetime belongs to intents, not scripts. A persistent Lua behavior can create persistent/until-time intents and replace/cancel its own named intents through the lifecycle transaction.

---

## 6. Approved Scripts Are Not Silently Rewritten

A context change is not permission to mutate an approved `LuaBehaviorScript`.

```text
runtime context changed
    -> existing approved behavior reacts through existing code/intents

behavior source should change
    -> explicit behavior revision workflow
```

A focus behavior can, for example, encode a deterministic grace period using an `UntilTime` intent or named intent replacement. Once encoded, Solid handles the timer without a model supervising it.

If future evidence says the source itself should change, the new source is an explicit revision that must be revalidated/evaluated and later reapproved.

A later revision milestone must define what happens to persistent intents owned by a previous approved script revision. Replacing source/revision alone is not sufficient lifecycle semantics.

---

## 7. What Liquid Must Eventually Expose

### Discover

Within an explicitly selected scope a caller should learn:

- available capability paths;
- exact Lua access expressions;
- current read/write permission;
- exact model-visible read and write shapes;
- bounded trusted descriptions/units where useful;
- copied values for readable capabilities;
- relevant Lua authoring-contract/version and limits.

A snapshot is data, not authority.

### Author

A caller should eventually submit exact Lua behavior source without direct world mutation authority.

The trusted path needs:

- exact source preservation;
- prospective scope chosen by host/application policy;
- structural/host validation;
- stale-scope detection;
- explicit new-behavior versus revision distinction.

The model does not choose arbitrary permissions.

### Observe

A caller should reconstruct current relevant truth without reading registries/raw event files.

A future model-facing view should answer:

- what behavior is being inspected;
- which live intents it owns;
- target/value/name/priority/lifetime;
- whether each intent is selected;
- what competitor currently wins a target and who owns it;
- authoritative observed external state;
- relevant command/report/evidence.

The caller does not need conversation memory to know current runtime truth.

### Validate and evaluate

Generated behavior remains untrusted candidate data.

```text
candidate Lua
    -> bounded schema/capability checks
    -> real Lua sandbox/codec validation
    -> deterministic isolated Solid simulation/evaluation
    -> structured evidence
```

Provider structured/constrained generation improves quality, not authority.

Simulation can demonstrate mechanical validity/scenario behavior. It cannot prove that an intervention is desirable for a person.

### Operate

Later callers may need to stop/alter live operation without rewriting source.

Such actions must be explicitly scoped and ownership-aware. Never expose generic equivalents of:

```text
mutate_world
write_any_component
destroy_any_intent
raw_registry_access
execute_arbitrary_native_code
```

Exact mutation surface is deferred until inspection/proposal/evaluation show what is actually needed.

### Explain

Liquid exposes structured causal facts, not natural-language explanations.

Example:

```text
FocusSupport wants office light ON
its persistent intent is alive but not selected
FollowUser currently wins with OFF
OFF was dispatched and confirmed
observed light is OFF
```

An external model/caller turns those facts into language.

---

## 8. Model-Facing Schema and Capability Manifest

The first Stage 2 gap exists because `LuaComponentCodec<T>` contains executable `encode` and `decode` functions that a model cannot introspect.

Importantly, those two directions are independent: Solid does not prove that the shape produced by `encode` is identical to the shape accepted by `decode`.

Therefore a model-visible Lua binding needs trusted metadata for **both directions**:

```text
readSchema  -> describes LuaComponentCodec<T>::encode output
writeSchema -> describes LuaComponentCodec<T>::decode input
```

A symmetric helper may reuse one schema for both when the codec genuinely has the same shape.

### `LuaValueSchema`

L0 describes values as they actually cross the Lua boundary rather than adopting full JSON Schema.

Exact V1 kinds:

- Boolean -> `bool`;
- Integer -> `std::int64_t`;
- Number -> finite `double`;
- String -> `std::string` with explicit bounds/optional enum;
- Array -> `LuaValue::Array` with one child schema/count bounds;
- Object -> `LuaValue::Table` with named required/optional fields and unknown fields rejected by default.

Integer/Number are not implicitly coerced in L0 because current `LuaValue` keeps them distinct.

Short bounded trusted descriptions may explain binding/field meaning or units. They do not alter validation or permission.

Do not initially add null, bytes, numeric coercion, unions/composition, `$ref`, recursive graphs, regex, arbitrary JSON Schema, or provider-specific keywords without a real current codec requirement.

The executable codec remains final validation. Schema disagreement is a trusted host/configuration bug, not permission to bypass the codec.

### Capability manifest

A `LuaCapabilityManifest` is an immutable copied view for a prepared behavior/current access state, built through the existing `LuaBehaviorRunner` binding boundary.

It exposes only what an external author needs:

- exact host-generated access expression;
- script-visible type/component name;
- current permission;
- `readSchema` + copied value when readable;
- `writeSchema` when writable;
- bounded trusted binding/schema descriptions;
- current monotonic capture time;
- stable Lua authoring-contract/version marker.

For a fully described binding:

| Permission | read side | write side |
| --- | --- | --- |
| Read | `readSchema` + value | absent |
| Write | absent | `writeSchema` |
| ReadWrite | `readSchema` + value | `writeSchema` |

Schema-less existing bindings remain executable for trusted/human scripts but do not automatically enter model discovery.

The manifest contains no raw component slots, pointers, registries, mutable world objects, credentials, or caller-selected owner.

### Exact Lua construction constraints matter

The model-facing contract must reflect real Lua representation details:

- Number and Integer are separate kinds;
- host-provided empty arrays preserve a private Array marker;
- literal `{}` is an empty Object, so a write-only empty Array cannot currently be constructed from `{}`;
- unusual access names require host-generated bracket/escaped paths.

Do not make schema metadata promise syntax the sandbox does not actually support.

---

## 9. Model and Agent Execution Are Application Choices

Two independent axes remain outside Liquid.

### Reasoning style

```text
direct inference
    context -> one bounded response

agent execution
    goal -> multiple model/tool turns -> result/action
```

### Deployment

Either style may use local or hosted models:

```text
Direct + Local
Direct + Hosted
Agent + Local
Agent + Hosted
```

Liquid Layer chooses. Liquid does not.

Hermes is a useful future consumer because it provides an agent loop, memory/sessions, scheduling, provider routing, bounded direct plugin LLM calls, and MCP integration. It remains optional/replaceable.

A model can be stateless between calls. Current runtime facts are reconstructed from Liquid/Solid; agent memory may preserve history/meaning but is not runtime truth.

---

## 10. Reaction, Revalidation, and Application Policy

Liquid does not define `focus`, `sensory overload`, `task initiation`, `sleep preparation`, or other application semantics.

Liquid Layer may use sensors/context/user input/model inference/deterministic policy/agents to decide that an existing behavior should start, continue, stop, or be reconsidered.

Whenever possible, semantic model reasoning should become deterministic approved Lua/intent policy.

Example:

```text
application/model interpretation:
    "brief absence should not end focus support"

approved behavior policy:
    temporary absence -> finite UntilTime grace policy
    return before deadline -> keep/restore persistent desire
    deadline passes -> Solid expires temporary intent normally
```

No generic `SemanticTrigger`, `BehaviorCondition`, or adaptive state-machine abstraction is currently accepted. Try components, Lua watches, named intents, intent lifetimes, and application orchestration first.

Liquid also does not wake an agent whenever an intent loses resolution by default. Losing is normal conflict behavior. Liquid exposes current truth; Liquid Layer decides which changes deserve inference.

---

## 11. Acceptance Scenarios

### FocusSupport

A prepared behavior wants a light state while the user is in a focus context.

It may own a persistent intent and encode temporary absence/grace behavior using existing lifetimes/named-intent replacement. Once encoded, Solid handles timing deterministically.

### FollowUser competing with FocusSupport

A second behavior wants an abandoned room light OFF.

If FollowUser wins:

```text
FocusSupport ON intent: alive
FocusSupport ON intent: not selected
FollowUser OFF intent: selected
observed light: OFF after authoritative feedback
```

If FollowUser stops wanting OFF, FocusSupport's original persistent intent may win again without reconstruction.

A future Liquid runtime view must represent these facts correctly.

### Stateless caller reconstruction

An agent can restart/change provider/lose context and reconstruct relevant runtime truth through Liquid instead of relying on remembered requests.

### Multiple AI strategies

The same semantic Liquid capability should work for a direct local model and for an external agent such as Hermes over MCP without changing authority rules.

---

## 12. MCP Is a Transport, Not the Architecture

```text
Liquid semantic API
      │
      ├── direct application use
      │
      └── MCP adapter
              │
              ├── Hermes
              └── other clients
```

Current MCP direction as of 28 August 2026:

- stable `2026-07-28` uses a stateless core;
- Sampling, Roots, and Logging are deprecated for new implementation;
- tool input/output schemas use JSON Schema 2020-12;
- authorization is a transport/security concern;
- official Tier 1 SDKs are TypeScript, Python, Go, and C#; no official C++ SDK currently exists.

Consequences:

- do not use MCP Sampling as an internal model-provider abstraction;
- render Liquid contracts into MCP schemas rather than make protocol schema types canonical engine types;
- keep tools focused/context-scoped;
- validate requests/results locally;
- tool annotations/descriptions are never authorization;
- re-evaluate implementation language/SDK only when the MCP milestone begins;
- prefer local/owner-controlled first integration unless real remote requirements justify auth/network complexity.

Hermes is one integration proof, not the contract owner.

---

## 13. Threading and Async Boundary

`World` and `Runtime` are single-thread-confined.

A future model/HTTP/MCP transport cannot safely retain a `Runtime&` and invoke it from arbitrary request threads.

Accepted direction:

> Model-facing data crosses the boundary as bounded immutable copies. Mutating requests are marshalled back to the host/Runtime owner-controlled path. Transport owns no Runtime authority.

This keeps model calls asynchronous/outside `Runtime::run_frame()`.

The exact snapshot publisher/request queue/host bridge/IPC mechanism is deferred until a milestone needs it.

A later runtime view must also define a consistent capture point so it does not combine live intents from one moment with stale resolver selections from another.

---

## 14. Evidence

Solid evidence answers:

> What did the deterministic runtime execute, select, command, receive, and observe?

Future Liquid evidence answers:

> Why was a behavior proposed, evaluated, revised, approved, rejected, or activated?

Do not casually add prompts, conversation history, proposal rationales, or provider metadata to Solid Event Format v1.

Liquid records may correlate to Solid session/behavior/evidence while remaining a separate adaptive-control record stream. Retention of user/model context is an explicit Liquid Layer/privacy decision.

---

## 15. Privacy and Security Principles

- Context is allowlisted/scoped, not dumped wholesale.
- A model sees only capabilities selected for its task/scope.
- Readable value never implies write authority.
- Read/write schema metadata never enlarges `World` permission.
- Raw slots, pointers, registries, credentials, adapter internals, unrelated users, and full event history are excluded by default.
- Repair/retry never gains authority because previous output failed.
- External agent memory is not current runtime truth.
- Provider constrained/structured output is not trusted host validation.
- Trusted descriptions are bounded registration metadata; arbitrary runtime strings stay structured data.
- Remote MCP auth/tool annotations never replace Solid ownership/permission checks.
- Model-facing values/diagnostics/manifests/schemas/tool results have explicit resource bounds.

---

## 16. Accepted Stage 2 Decisions

- Solid v0.1.0 remains the frozen deterministic foundation.
- Liquid is a model-facing semantic control/authoring layer over Solid, not an AI provider/runtime.
- Liquid Layer chooses inference/agent strategy and owns application meaning/policy.
- Local/hosted is independent from direct/agent execution.
- No `ModelPort` unless a future reusable Liquid feature proves engine-owned inference is required.
- Hermes is optional/replaceable.
- MCP is a future transport adapter, not internal API.
- Lua remains the generated executable behavior boundary for initial Stage 2 milestones.
- Approved source is not silently rewritten because context changed.
- Intent lifetime remains intent metadata.
- A losing live intent remains alive and may win later.
- Liquid exposes current truth rather than notifying an agent for every normal resolution change.
- Model-facing discovery requires trusted machine-readable **read and write** Lua value schemas because codec directions can differ.
- L0 remains inside existing `Liquid::Lua`; no speculative adaptive/provider/agent/MCP target/folder.

---

## 17. Deferred / Rejected Directions

### LLM inside `Runtime::run_frame()`
Rejected.

### Hermes as a core dependency
Rejected.

### MCP as Liquid's object model
Rejected.

### Full JSON Schema as canonical Liquid value schema
Rejected for L0.

### Provider structured output as authority
Rejected.

### New behavior IR before Lua evidence
Deferred.

### Generic semantic-trigger engine
Deferred until repeated scenarios prove a missing primitive.

### Continuous agent notification for every lost intent selection
Rejected as default.

### Provider/model selection inside Liquid
Rejected for current Stage 2.

---

## 18. Documentation Authority

For current development:

1. `AGENTS.md` — active milestone, allowed scope, coding-agent rules;
2. `DEVELOPMENT_TRACKING.md` — milestone status/order/evidence;
3. `docs/LIQUID_STAGE2_PLAN.md` — detailed Stage 2 rationale/roadmap/research;
4. `docs/LIQUID_L0_IMPLEMENTATION_SPEC.md` — exact current L0 semantics/test expectations;
5. `COMPLETE_SOLID.md` — accepted Solid completion audit;
6. focused `docs/*.md` contracts — Solid public API, Lua, threading, effects, events, replay, security;
7. this document — conceptual architecture/vocabulary.

When implementation proves an accepted concept wrong, update the architecture explicitly rather than silently coding around it.
