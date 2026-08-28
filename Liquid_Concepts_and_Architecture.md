# Liquid Concepts and Architecture v0.4

**Status:** Accepted Stage 2 architecture baseline  
**Date:** 28 August 2026  
**Foundation:** Solid v0.1.0  
**Current implementation milestone:** L0 — Model-Facing Lua Capability Contract

---

## 1. Purpose

This document defines the conceptual architecture and vocabulary of the Liquid project after completion of Solid v0.1.0 and the Stage 2 design discussion.

It answers three questions:

1. what Solid already owns and must continue to own;
2. what Liquid adds on top of Solid;
3. what remains application policy in Liquid Layer.

Implementation order, allowed files, milestone evidence, rejected alternatives, and current external research are tracked in `docs/LIQUID_STAGE2_PLAN.md`. Operational coding-agent scope is in `AGENTS.md`.

---

## 2. Project Layers

### Solid

Solid is the completed deterministic execution foundation.

It owns runtime truth: behaviors, component access, immutable intents, intent lifetime, deterministic conflict resolution, frame execution, effects, feedback, authoritative observations, durable evidence/replay, bounded Lua lifecycle execution, and simulation.

Solid v0.1.0 is complete through M1-M6 and finalization S0-S7.

### Liquid

Liquid is the reusable **model-facing control and authoring layer over Solid**.

Its purpose is to make Solid safely understandable and operable by external models or agent runtimes without making those models part of the deterministic Runtime.

Liquid provides the semantic surface needed to:

- discover bounded behavior capabilities;
- author deterministic Lua behavior proposals;
- validate and evaluate proposals;
- inspect current runtime truth;
- eventually perform explicitly bounded operations;
- expose structured causal evidence;
- later render the same surface through transports such as MCP.

Liquid does **not** choose the model/provider, own a conversation loop, decide when inference should occur, or define neurodivergent-support policy.

### Liquid Layer

Liquid Layer is the final adaptive smart-environment application/research system.

It owns meaning and application policy:

- user goals and preferences;
- sensors and environment context;
- deciding when a model/agent should be invoked;
- choosing local versus hosted inference;
- choosing direct model calls versus an agent runtime;
- conversation and user-memory policy;
- neurodivergent-support scenarios;
- real smart-home/device integration;
- consent/privacy/user-facing control.

Liquid Layer uses Liquid in the same way Liquid uses Solid: as a reusable lower-level tool with explicit boundaries.

---

## 3. Architectural Thesis

> Liquid lets probabilistic systems understand, synthesize, inspect, and negotiate behavior while Solid remains the deterministic authority that executes, constrains, resolves, audits, and reproduces it.

The intended direction is:

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

A generated or approved behavior must continue to execute correctly if every LLM and agent is unavailable.

---

## 4. Solid Authority Is Not Duplicated

Liquid must compose model-facing views from Solid's existing sources of truth. It must not maintain a second runtime or a competing shadow state.

The following distinctions are fundamental:

```text
live intent / desire
    ≠ selected intent
    ≠ dispatched command
    ≠ adapter report
    ≠ authoritative observed state
```

A selected desire does not prove that an external device changed. Externally controlled component state changes only after correlated, validated authoritative feedback or a revisioned external observation.

Similarly:

```text
intent alive
    ≠ intent currently selected
```

A persistent intent that loses a conflict remains alive. If the winning competitor later disappears, the older live intent may become selected again without being recreated and without invoking a model.

This property is important for adaptive applications because models do not need to continuously reconstruct preferences that Solid already preserves correctly.

---

## 5. Behaviors, Lua, and Intents

### Behavior

A behavior is Solid's main runtime identity and owns the intents created on its behalf.

Liquid may create, inspect, evaluate, revise, or later operate behaviors through approved boundaries, but does not replace Solid's behavior identity model.

Do not assume an external adaptive agent itself must be a Solid behavior or carry an `Agent` component. An agent runtime may live entirely outside Solid and consume the Liquid API.

### Lua behavior source

`LuaBehaviorScript` remains the first generated executable behavior representation.

This is deliberate, not temporary glue. Solid's Lua boundary was built for generated behavior:

- the host fixes the executing behavior owner;
- the host fixes monotonic runtime time;
- each execution receives a fresh Lua VM;
- persistent script state must live in ordinary codec-backed components;
- Lua sees copied snapshots only;
- access is typed, named, and allowlisted;
- writable capabilities expose proposal closures rather than mutation authority;
- lifecycle proposals/cancellations/watches commit transactionally;
- instruction, memory, source, value, table, string, intent, cancellation, watch, and diagnostic budgets are bounded;
- `World`, `Coordinator`, registries, storage, raw slots/pointers, OS/I/O/package/debug authority, arbitrary native execution, and owner selection are not exposed.

The detailed executable contract is normative in `docs/LIFECYCLE_SCRIPTING.md` and the current public scripting headers.

A separate declarative behavior IR is deferred until real model-generation/evaluation evidence demonstrates a problem Lua cannot solve cleanly.

### Intent

An intent is an immutable proposed state/effect owned by a behavior.

Current lifetime policies are:

- `Persistent`;
- `UntilTime` using monotonic session-relative `IntentTime`.

Explicit cancellation destroys the intent.

Intent lifetime belongs to intents, not scripts. A persistent Lua behavior can create persistent or until-time intents and can replace/cancel its named intents through the existing lifecycle transaction.

---

## 6. Approved Scripts Are Not Silently Rewritten

A change in environment context is not itself permission to mutate an approved `LuaBehaviorScript`.

The system distinguishes:

```text
runtime context changed
    -> existing approved behavior reacts through its existing code/intents

behavior source should change
    -> explicit behavior revision workflow
```

For example, a focus behavior may encode a deterministic grace period using an `UntilTime` intent or named intent replacement. The model that designed the policy does not need to supervise the timer.

If future evidence shows that the behavior itself should be rewritten, that is a deliberate revision that must be revalidated/evaluated and eventually reapproved.

A later revision milestone must explicitly define what happens to persistent intents owned by the previous approved revision; merely replacing Lua source is not enough to prove safe lifecycle semantics.

---

## 7. What Liquid Must Eventually Expose

Liquid's target semantic surface is grouped by responsibility rather than protocol.

### Discover

A caller should be able to learn, within an explicitly selected scope:

- what capabilities are available;
- exact Lua access paths;
- current read/write permission;
- exact model-visible value shape;
- copied readable values;
- relevant Lua authoring contract/version and limits.

A snapshot is data, never authority.

### Author

A caller should be able to submit an exact Lua behavior proposal without receiving direct world mutation authority.

The proposal path eventually needs:

- exact source preservation;
- prospective capability scope chosen by trusted host/application policy;
- structural/host validation;
- stale-scope detection;
- explicit distinction between new behavior and revision.

### Observe

A caller should be able to reconstruct relevant current truth without reading registries or raw event files.

A model-friendly view should eventually answer questions such as:

- what behaviors are relevant/currently present;
- which live intents a behavior owns;
- intent target/value/name/priority/lifetime;
- whether each intent is selected;
- what competing intent currently wins a target;
- what behavior owns that winner;
- what external state is authoritatively observed;
- what relevant command/report/evidence led to that state.

The caller should not need conversational memory to know current runtime truth.

### Validate and evaluate

Generated behavior must be checked through trusted code.

The long-term path is:

```text
candidate Lua
    -> bounded schema/capability checks
    -> real Lua sandbox validation
    -> deterministic isolated Solid simulation/evaluation
    -> structured evaluation evidence
```

Simulation can demonstrate mechanical validity and behavior under declared scenarios. It does not prove that an intervention is desirable for a person.

### Operate

Some later use cases need an agent to stop or alter live operation without rewriting source.

Any such operation must be explicitly scoped and ownership-aware. Liquid must not expose generic primitives equivalent to:

```text
mutate_world
write_any_component
destroy_any_intent
raw_registry_access
execute_arbitrary_native_code
```

The exact mutation surface is deliberately deferred until read-only inspection and proposal evaluation reveal what operations are actually necessary.

### Explain

Liquid should expose structured causal facts, not synthesize natural-language explanations itself.

A model may receive facts equivalent to:

```text
FocusSupport wants office light ON
its persistent intent is alive but not selected
FollowUser currently wins with OFF
OFF was dispatched and confirmed
observed light state is OFF
```

The caller/model can turn those facts into language.

---

## 8. Model-Facing Schema and Capability Manifest

The first Stage 2 gap exists because `LuaComponentCodec<T>` contains executable C++ `encode`/`decode` functions. A model cannot introspect those functions to discover required fields, ranges, or enums.

Therefore model-visible Lua bindings need trusted machine-readable metadata beside the executable codec.

### `LuaValueSchema`

The Stage 2 L0 schema describes values as they actually cross the Lua boundary. It is intentionally smaller than JSON Schema.

V1 vocabulary:

- Boolean;
- signed Integer with optional bounds;
- finite Number with optional bounds;
- String with explicit size bounds and optional enum;
- Array with one item schema and count bounds;
- Object with named required/optional fields and unknown fields rejected by default.

Optional short bounded descriptions may explain component/field semantics or units. They are trusted registration metadata, not arbitrary runtime text.

Do not initially add null, bytes, unions/composition, `$ref`, recursive graphs, regex constraints, arbitrary JSON Schema, or provider-specific keywords without a real current codec requirement.

The executable codec remains final value validation. Schema disagreement is a trusted host/configuration bug, not permission for a model to bypass the codec.

### Capability manifest

A `LuaCapabilityManifest` is an immutable copied view for a specific prepared behavior/current access state.

It should expose only what the caller needs to author against the real Lua boundary:

- exact host-generated access expression;
- script-visible type name and component name;
- current permission;
- copied readable value when allowed;
- writable schema when allowed;
- current monotonic time when relevant;
- stable authoring-contract/version marker and bounded contract metadata.

Permission projection is exact:

| Permission | readable value | writable schema |
| --- | ---: | ---: |
| Read | yes | no |
| Write | no | yes |
| ReadWrite | yes | yes |

The manifest never contains authority-bearing pointers, mutable registries, raw component slots, or a caller-selected owner.

The host generates Lua paths itself, including bracket/escaping forms for unusual names. Models should copy the provided path rather than reconstruct it.

---

## 9. Model and Agent Execution Are Application Choices

The architecture separates two independent axes.

### Reasoning style

```text
direct inference
    prompt/context -> one bounded response

agent execution
    goal -> multiple model/tool turns -> result/action
```

Direct inference is appropriate for bounded classification/interpretation/extraction. Agent execution is useful for iterative tool use, repair, conversation, persistent memory, or orchestration.

### Deployment

Either reasoning style may use:

```text
local model
or
hosted model
```

Therefore all four combinations are valid:

```text
Direct + Local
Direct + Hosted
Agent + Local
Agent + Hosted
```

Liquid does not decide among them. Liquid Layer does.

Hermes Agent is a useful future consumer because it provides an agent loop, memory/sessions, scheduling, provider routing, direct structured plugin LLM calls, and MCP integration. It remains optional and replaceable.

A model may be stateless between calls. The system must not be. Current runtime facts are reconstructed from Liquid/Solid whenever the caller needs them; agent memory may preserve meaning/history but is not runtime truth.

---

## 10. Reaction, Revalidation, and Application Policy

Liquid does not define what concepts such as `focus`, `sensory overload`, `task initiation`, or `sleep preparation` mean.

Liquid Layer may use sensors, context, user input, model inference, deterministic policy, or an agent to decide that an existing behavior should start, continue, stop, or be reconsidered.

Whenever possible, a semantic model decision should become deterministic policy inside approved Lua/intents.

Example:

```text
model/application interprets:
    "brief absence should not end focus support"

approved Lua/intent policy:
    absence -> temporary UntilTime grace-period intent
    return before deadline -> restore/keep persistent intent
    deadline passes -> Solid expires temporary state normally
```

This avoids asking an LLM the same deterministic question every frame/minute.

No generic `SemanticTrigger`, `BehaviorCondition`, or adaptive state-machine abstraction is currently accepted. Existing components, Lua watches, named intents, intent lifetimes, and application orchestration must be tried first. A new abstraction should be added only after multiple distinct scenarios demonstrate the same missing engine primitive.

Liquid also does not wake an agent whenever an intent loses resolution by default. Losing is normal Solid conflict behavior. Liquid should make current state inspectable; Liquid Layer decides which changes deserve inference.

---

## 11. Acceptance Scenarios

These examples test the abstraction but are not hard-coded Liquid concepts.

### FocusSupport

A prepared behavior wants a light state while the user is in a focus context.

The behavior may own a persistent intent and encode temporary absence/grace-period policy using existing lifetimes/named-intent replacement. Once encoded, Solid handles the timer deterministically.

### FollowUser competing with FocusSupport

A second behavior follows the user between rooms and wants an abandoned light OFF.

If FollowUser wins:

```text
FocusSupport ON intent: alive
FocusSupport ON intent: not selected
FollowUser OFF intent: selected
observed light: OFF after authoritative feedback
```

If FollowUser later stops wanting OFF, the original persistent FocusSupport intent may win again without reconstruction.

A future Liquid runtime view must represent this correctly.

### Stateless caller reconstruction

An agent process may restart, change provider/model, or lose conversational context. When called again it should be able to reconstruct relevant runtime truth through Liquid rather than rely on what it remembers having requested previously.

### Multiple AI strategies

The same semantic Liquid capability should be usable by a direct local model chosen by Liquid Layer and by an external agent such as Hermes through an MCP adapter. The authority rules do not change based on who calls.

---

## 12. MCP Is a Transport, Not the Architecture

MCP is the intended first agent-facing transport, but Liquid's semantic API must be independently testable without MCP serialization/networking.

Relationship:

```text
Liquid semantic API
      │
      ├── direct application use
      │
      └── MCP adapter
              │
              ├── Hermes
              └── other MCP clients
```

The current MCP direction as of 28 August 2026:

- stable revision `2026-07-28` uses a stateless protocol core;
- Sampling, Roots, and Logging are deprecated for new implementations;
- tool input/output schemas use JSON Schema 2020-12;
- authorization has been hardened and is a transport concern;
- official Tier 1 SDKs are TypeScript, Python, Go, and C#; no official C++ SDK currently exists.

Consequences for Liquid:

- do not use MCP Sampling as an internal model-provider abstraction;
- render Liquid contracts into MCP schemas rather than make MCP schema types canonical engine types;
- keep tools focused and context-scoped;
- validate tool arguments/results locally;
- treat tool annotations/descriptions as hints, not authority;
- re-evaluate implementation language/SDK only when the MCP milestone begins;
- prefer a local/owner-controlled first integration unless a real remote-use requirement justifies OAuth/network complexity.

Hermes should be one integration proof, not the owner of the MCP contract.

---

## 13. Threading and Async Boundary

`World` and `Runtime` are single-thread-confined by the Solid v0.1 contract.

A future model/HTTP/MCP transport cannot safely keep a `Runtime&` and invoke it from arbitrary request threads.

The accepted direction is:

> Model-facing data crosses the boundary as bounded immutable copies. Mutating requests are marshalled back to the host/Runtime owner-controlled path. The transport itself owns no Runtime authority.

This also helps keep model calls asynchronous and outside `Runtime::run_frame()`.

The exact snapshot publisher, request queue, host bridge, or IPC mechanism is deferred until a transport/operation milestone needs it.

---

## 14. Evidence

Solid evidence answers:

> What did the deterministic runtime execute, select, command, receive, and observe?

Future Liquid evidence answers a different question:

> Why was a behavior proposed, evaluated, revised, approved, rejected, or activated?

Do not casually add model prompts, conversation history, proposal rationales, or provider metadata to Solid Event Format v1.

Future Liquid records may correlate to Solid session/behavior/evidence while remaining a separate adaptive-control record stream. What model/user context is retained is a Liquid Layer/privacy decision and must be explicit.

---

## 15. Privacy and Security Principles

- Context is allowlisted/scoped, not dumped wholesale.
- A model sees only capabilities selected for the current authoring/inspection task.
- A readable value never implies write authority.
- Model-visible schema metadata never enlarges `World` permission.
- Raw slots, pointers, registries, credentials, adapter internals, unrelated users, and full event history are excluded by default.
- Repair/retry attempts never receive extra authority merely because earlier model output failed.
- External agent memory is not trusted as current runtime truth.
- Model/provider constrained output is useful for quality but is not trusted validation; Liquid/Solid validate locally.
- Host-provided descriptions are bounded trusted metadata. Arbitrary runtime strings remain data and must be structurally encoded/delimited by future renderers.
- Remote MCP authorization and tool annotations never replace Solid ownership/permission checks.
- Model-facing values, diagnostics, manifests, schemas, and later tool results must have explicit resource bounds.

---

## 16. Accepted Stage 2 Decisions

- Solid v0.1.0 remains the frozen deterministic foundation.
- Liquid is a model-facing semantic control/authoring layer over Solid, not an AI provider/runtime.
- Liquid Layer chooses the inference/agent strategy and owns application meaning/policy.
- Local versus hosted inference is independent from direct versus agent execution.
- No `ModelPort` is added unless a future reusable Liquid feature proves the engine itself must call a model.
- Hermes is optional and replaceable.
- MCP is a future transport adapter, not Liquid's internal API.
- Lua remains the generated executable behavior boundary for the first Stage 2 milestones.
- Approved Lua source is not silently rewritten because context changed.
- Intent lifetime remains intent metadata; scripts do not gain their own lifetime model merely for Liquid.
- A losing live intent remains alive and may win again later.
- Liquid should expose current truth rather than continuously notify an agent of every normal resolution change.
- Model-facing capability discovery requires trusted machine-readable Lua value schemas.
- The first approved implementation milestone is L0: schema + capability manifest inside the existing `Liquid::Lua` boundary.
- No speculative adaptive/agent/provider/MCP directory or exported target is created in L0.

---

## 17. Deferred / Rejected Directions

### LLM inside `Runtime::run_frame()`

Rejected. It breaks deterministic frame ownership, latency/failure isolation, and replay expectations.

### Hermes as a core dependency

Rejected. Approved behavior must keep running without Hermes, and other agents must be able to consume the same Liquid surface.

### MCP as Liquid's internal object model

Rejected. Protocol evolution must not dictate engine semantics.

### Full JSON Schema as the canonical Liquid value schema

Rejected for L0. Liquid needs a small schema matching the actual Lua value vocabulary and can render outward schemas later.

### New behavior IR before Lua evidence

Deferred until concrete model-generation problems justify it.

### Generic semantic-trigger engine

Deferred until multiple scenarios prove an engine-level primitive is missing.

### Continuous agent notification for every lost intent selection

Rejected as a default. Conflict/loss is normal runtime behavior; applications decide which changes matter.

### Provider/model selection inside Liquid

Rejected for current Stage 2. This is application policy.

---

## 18. Documentation Authority

For current development, use this order:

1. `AGENTS.md` — active milestone, allowed scope, coding-agent rules;
2. `DEVELOPMENT_TRACKING.md` — milestone status/order/evidence;
3. `docs/LIQUID_STAGE2_PLAN.md` — detailed Stage 2 design rationale, roadmap, research;
4. `COMPLETE_SOLID.md` — accepted Solid completion audit;
5. focused `docs/*.md` contracts — Solid public API, threading, Lua, effects, events, replay, security;
6. this document — conceptual architecture and vocabulary.

When implementation reveals that an accepted concept is wrong, update the architecture explicitly rather than silently coding around it.