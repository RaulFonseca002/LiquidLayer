# Liquid Stage 2 Plan

Related research: [Semantic invariance in Liquid and MCP](SEMANTIC_INVARIANCE_RESEARCH.md)
proposes evaluating paraphrases against verified outcomes and authority, with
meaning-changing controls. It does not activate or expand an implementation milestone.

**Design revision:** 6–7 September 2026 documentation hardening.
**Status:** L0–L6 specified, not implemented; no step is active until the owner
separately activates one.
**Code foundation:** Solid v0.1 plus the pre-Liquid hardening now on `main`
(`5979e14`, follow-up `2aee8a4`; see [tracking](../DEVELOPMENT_TRACKING.md)).
The dated design-review baselines are in
[the validation report](LIQUID_DOCUMENTATION_VALIDATION.md).

## Purpose and boundary

Liquid lets a trusted application expose bounded capabilities to an external
author, evaluate exact Lua proposals, inspect current truth, and approve and
operate behaviors. Solid remains the deterministic execution authority.
The author may be a human, a direct model call, or an external agent.

| Layer | Owns | Does not own |
| --- | --- | --- |
| Solid | World/behavior/access, immutable intents, lifecycle execution, resolution, effects, observed state, replay | Model selection or adaptive approval |
| Liquid | Scoped authoring, proposal/evaluation records, inspection, bounded approved operations | Inference, user meaning, a second Runtime |
| Liquid Layer | Context, consent, model invocation, hardware, user policy and studies | Bypassing Solid validation |

Local/hosted and direct/agentic are independent application choices. Approved
behavior keeps running if all models disappear. No provider dependency,
automatic repair loop, semantic-trigger engine or new behavior language is
needed for this Stage 2 slice.

## Decisions changed by validation

1. **Specify all milestones now, activate them separately.** The old provisional
   outlines do not provide enough information for a sequential implementation.
2. **Keep scope outside the live World.** A prospective author must not gain a
   live behavior merely to discover capabilities. L1 uses host-owned grants and
   runner-backed snapshots; L2 constructs an isolated evaluation World.
3. **Make multi-script lifecycle execution explicit.** Solid's current fixed
   script-component name is retained for old callers; L2 adds an opt-in
   per-behavior selection mode for independently authored scripts.
4. **Separate current truth from historical frame results.** A current losing
   intent and a selection recorded in an earlier frame are different facts.
5. **Put evidence before activation and transport last.** L4 supplies the journal
   used by L5. L6 adapts already-tested semantics to local MCP.
6. **Use host-owned exact artifacts for approval.** Session-scoped IDs locate
   immutable records; exact source and scope equality are authority. Solid's
   diagnostic FNV source identifier is not an approval signature.
7. **Replace behavior instances on revision.** Retire the old instance and its
   intents; install a fresh instance with reset lifecycle state. Retain the
   Liquid logical reference. No silent survival of obsolete persistent intents.
8. **Bound the first release honestly.** Adaptive records are session-local,
   in-memory and finite. No crash-safe adaptive recovery, permanent audit trail,
   network service or indefinitely bounded Solid checkpoint promise is made.

## Implementation sequence

Read [the common contract](LIQUID_IMPLEMENTATION_CONTRACT.md) with every spec.
The order below is the canonical replacement for the old provisional ladder.

| Milestone | Smallest complete outcome | Exit evidence |
| --- | --- | --- |
| [L0](LIQUID_L0_IMPLEMENTATION_SPEC.md) | Exact schemas and runner-built manifest for an existing behavior | Asymmetric codecs, permissions, paths, limits and legacy compatibility |
| [L1](LIQUID_L1_IMPLEMENTATION_SPEC.md) | Host-selected prospective scope and immutable source proposal | No live topology mutation, stale-target/policy rejection, isolated callers |
| [L2](LIQUID_L2_IMPLEMENTATION_SPEC.md) | Evaluate through real Runtime/Lua/InMemoryAdapter | Repeated scenario equivalence, complete rollback and independent scripts |
| [L3](LIQUID_L3_IMPLEMENTATION_SPEC.md) | Bounded scoped current view plus labeled frame evidence | Losing intent remains live, hidden competitors stay private, stale selections labeled |
| [L4](LIQUID_L4_IMPLEMENTATION_SPEC.md) | Queryable bounded authoring journal | Correlation, pagination, capacity admission and honest retention semantics |
| [L5](LIQUID_L5_IMPLEMENTATION_SPEC.md) | Approve/activate/replace/stop exact reviewed artifacts | Stale approval rejection, explicit intent cleanup, failure outcomes and idempotency |
| [L6](LIQUID_L6_IMPLEMENTATION_SPEC.md) | Optional local stdio MCP adapter | SDK and independent protocol tests against the same semantic backend |

L0 stays in `Liquid::Lua`. L1 introduces opt-in `Liquid::Authoring`, depending
on Core/Lua/Simulation, with `LIQUID_BUILD_AUTHORING=OFF` by default. This is an
explicit additive package proposal; existing three-component installations
remain unchanged when it is disabled. L6 tooling is not installed/exported.

## Acceptance scenarios

These are deterministic engineering fixtures, not recommendations about a
person's environment. Host fixtures use `Light{brightness: 0..100}` and a
small internal state component; applications choose real meanings and values.

- **FocusSupport:** create persistent brightness 70; model availability has no
  bearing on subsequent execution.
- **FollowUser:** competing higher-priority brightness 0 wins. FocusSupport's
  original intent remains live; after the competitor is cancelled it wins again.
  Physical brightness changes only after validated feedback.
- **Revision:** replace FocusSupport with source proposing brightness 30. Its
  old 70 intent is destroyed, even if it was losing at replacement time.
- **Stale scope:** revoke a grant, replace a target with the same name, change
  policy, or revise the managed behavior after evaluation. Old approval fails.
- **Failure:** malformed Lua, codec mismatch, exhausted bounds, failed frames,
  unavailable fixtures and partial topology cleanup never report success.
- **Stateless caller:** discard conversation memory; recover permitted current
  facts and session history from Liquid, without exposing other scopes.
- **Transport parity:** direct host calls and MCP calls return equivalent
  semantic results; protocol annotations never expand grants.

## Solid extensions and rejected shortcuts

Beyond the specified runner additions, L2 adds lifecycle selection and L3 adds
bounded read-only Core inspection. Their compatibility
and regression requirements are in those specs. L5 composes existing lifecycle
operations and explicitly handles their non-transactional topology failures.
It does not invent a World transaction or promise physical rollback.

Rejected: privileged live discovery behaviors, a parallel capability registry,
scope guessed from values, shared lifecycle source for unrelated behaviors,
FNV hashes as approval authentication, inferred physical success, direct remote
World mutators, evaluation that invokes production adapters, and moving model
work into frame execution.

Deferred beyond Stage 2: persistent adaptive journal/recovery, generic event
subscriptions, remote MCP/OAuth, automatic activation policy, model providers,
real devices, Scope repository split, and a new checkpoint retention design.
Each requires a separate owner-approved milestone, not an empty placeholder
inside L0–L6.

## Readiness and advancement

The [workflow](HERMES_MULTI_MODEL_DEVELOPMENT_GUIDE.md) defines the Claude →
tests → Codex review → owner loop. Every step has a spec, file allowlist,
test oracle and commands. A finding that invalidates a decision reopens that
contract; it never silently becomes implementer discretion.

The required pre-Liquid Solid fixes have landed on `main`. Before L0
activation: the owner records the activation in tracking, the docs are
reconciled to that exact base, and baseline checks pass on it. Neither this
document nor successful documentation validation marks future implementation
tests as passed.

## Appendix: external research checked on 28 August 2026

*Historical and non-normative.* This research informed the original Stage 2
plan and is kept for provenance. It is dated; recheck sources before relying on
any claim. The normative L6 transport and dependency pins live in
[the L6 spec](LIQUID_L6_IMPLEMENTATION_SPEC.md).

- **MCP.** `2026-07-28` introduced a stateless core, header routing, cacheable
  discovery/list results, authorization hardening, extensions, and updated
  Tier 1 SDKs. Sampling/Roots/Logging are deprecated for new implementations;
  direct provider integration replaces Sampling. Tool schemas support JSON
  Schema 2020-12. Sources:
  <https://blog.modelcontextprotocol.io/posts/2026-07-28/>,
  <https://blog.modelcontextprotocol.io/posts/2026-07-28-release-candidate/>,
  <https://ts.sdk.modelcontextprotocol.io/v2/>, <https://modelcontextprotocol.io/>.
- **Home Assistant.** Focused semantic LLM APIs are registered independently
  and can then be exposed over MCP. Architectural lesson: semantic API first,
  transport second. Source: <https://developers.home-assistant.io/docs/core/llm/>.
- **Hermes Agent.** The docs distinguished full agent execution from bounded
  direct plugin LLM calls and supported MCP protocol-era negotiation, including
  2026 stateless mode and self-hosted providers. Sources:
  <https://hermes-agent.nousresearch.com/docs/developer-guide/plugin-llm-access>,
  <https://hermes-agent.nousresearch.com/docs/reference/mcp-config-reference>,
  <https://hermes-agent.nousresearch.com/docs/integrations/providers>.
- **Structured/self-hosted inference.** OpenAI APIs, llama.cpp and vLLM provide
  structured/constrained outputs and tool calling in different forms and
  practical schema subsets, so Liquid keeps its canonical contract
  provider-neutral and validates locally. Sources: <https://developers.openai.com/>,
  <https://github.com/ggml-org/llama.cpp/tree/master/tools/server>,
  <https://docs.vllm.ai/en/latest/features/structured_outputs/>.
