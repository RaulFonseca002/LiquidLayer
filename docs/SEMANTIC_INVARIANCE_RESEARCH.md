# Semantic invariance in Liquid and MCP

**Research date:** 10 September 2026.

**Status:** research recommendation; no milestone activation or implementation.

## Finding

Equivalent natural-language requests should preserve the correct outcome and
authority under equivalent context. They do not necessarily require identical
response wording or tool-call sequences. This property is called semantic
invariance, paraphrase robustness, or function-calling robustness.

For example, “How much does the user have in the account?” and “What is the
current value in the account?” should retrieve the same fact when account,
balance definition, currency, and snapshot are fixed. Otherwise, current
portfolio valuation and available cash may legitimately differ. Clarifying
ambiguous meaning is part of correctness.

The recommendation is to evaluate this at Liquid's external author/application
boundary. Solid determinism and authority remain unchanged.

## Evidence and limits

- **On the Robustness of Agentic Function Calling**, Rabinovich and Anaby
  Tavor, TrustNLP 2025, is the closest direct match. It tests meaning-preserving
  query variations and related distractor functions in an expanded toolkit.
  Its BFCL-derived, single-turn evaluation identifies weaknesses in tool-use
  robustness. It does not establish current model reliability or Liquid's
  performance. [Paper](https://aclanthology.org/2025.trustnlp-main.20/)
- **Semantic Invariance in Agentic AI**, March 2026 preprint, applies
  metamorphic testing: transform inputs while preserving meaning, then compare
  results. It tests seven models on 19 scientific reasoning problems with eight
  transformations. This supports the method, but is not an MCP execution
  benchmark. Its small corpus does not establish smart-environment reliability.
  [Paper](https://arxiv.org/abs/2603.13173)
- **MCP-Atlas** scores grounded factual claims and permits valid alternative
  tool-call trajectories. This supports outcome-based evaluation rather than
  one exact expected transcript. For Liquid, additionally checking authority
  and side effects is our design inference, not a guarantee from this benchmark.
  [Authors' description](https://labs.scale.com/papers/mcpatlas)
- **MCP's tool specification** defines names, schemas, discovery, calls, and
  results. Our inference is that protocol conformance cannot establish correct
  interpretation: a well-formed call can select the wrong operation or target.
  The cited version is 2025-11-25; this research does not validate later SDK or
  protocol versions proposed elsewhere in the documentation.
  [Versioned specification](https://modelcontextprotocol.io/specification/2025-11-25/server/tools)

## Documentation baseline

Reviewed committed documentation at `76f00e4`, shared by the local
`docs/liquid-stage2-plan` and `docs/liquid-documentation-hardening` branches at
research time, plus the pending documentation-hardening worktree drafts.
The `fix/pre-liquid-hardening` documentation was not the design source.

The committed [Stage 2 plan](LIQUID_STAGE2_PLAN.md) uses the older provisional
ladder with MCP at L4. The pending hardening draft moves the journal to L4 and
MCP to L6. Those pending specifications are not published or approved by adding
this research note. References below describe both baselines explicitly;
milestone numbers must follow the accepted roadmap when tests are implemented.

## Fit with Liquid

| Existing or proposed boundary | What it provides | What it does not establish |
| --- | --- | --- |
| L0 schemas, trusted descriptions, exact Lua paths | Explicit shapes, meanings, and accessible capabilities | That a schema-valid proposal expresses the user's request |
| Proposed L2 host-owned fixtures and assertions | Independent expected outcomes through the real runtime path | Robust interpretation of natural-language variants |
| Pending L2 normalized semantic traces | Repeated execution comparison despite fresh handles | General equivalence of different generated programs |
| Proposed L3 scoped current view | Separation of live desires, selections, commands, and observed truth | Correct model interpretation of those distinctions |
| Pending L4 bounded journal | Artifact and evaluation provenance | Reproduction of model reasoning |
| Pending L6 five fixed MCP tools | Focused discovery, proposal, evaluation, inspection, and history | Correct tool selection from any wording |

The [L0 specification](LIQUID_L0_IMPLEMENTATION_SPEC.md) already separates
schema metadata from executable codec and permission authority. Preserve this:
a valid brightness value can still be the wrong brightness for the request.

The pending L6 draft excludes approval, activation, replacement, stop, grants,
and frame control from MCP. Paraphrasing must never expand authority. The
pending journal excludes raw prompts by default; a future evaluation dataset
does not silently authorize storing conversations in that journal.

## Three separate properties

1. **Correctness:** every result meets an independently defined expected outcome.
2. **Invariance:** meaning-preserving variants continue to meet that outcome.
3. **Sensitivity:** meaning-changing variants produce the appropriately different
   result or clarification.

A consistently wrong answer passes a consistency-only test. Conversely, changed
observations or permissions can legitimately change the answer. Freeze context
for invariance tests and evaluate context changes separately.

Compare operation, target, values/units, time scope, evidence, permissions, and
side effects. Ignore harmless response phrasing and read-call ordering where
the task permits it. Extra proposal submissions are not harmless variations:
they create artifacts and consume bounded session capacity.

Temperature zero, schema validation, embedding similarity, and deterministic
replay do not prove semantic correctness. Replay explains what accepted code
did; it does not prove the code captured the user's meaning.

## Proposed acceptance examples

Use a frozen lighting fixture: FocusSupport requests persistent brightness 70,
a higher-priority competitor requests 0, and feedback is delayed. Specify the
last authoritative observation independently from the desired values.

| Requests | Expected relationship |
| --- | --- |
| “What brightness is actually observed?” / “What is the last confirmed brightness?” | Same observation and freshness; no substitution of a pending command |
| “What does FocusSupport want?” / “Which brightness is it requesting?” | Same readable live desired value, 70 |
| “Did its intent disappear when it lost?” / “Is its request still alive despite being overridden?” | Same fact: losing selection does not cancel a live intent |
| “Which request won?” / “What is physically observed?” | Different meanings; selection is not observation |
| “Inspect the light” / “Change the light” | Different operations; inspection must not create a proposal |
| “Set brightness to 70” / “Increase brightness by 70” | Absolute and relative requests must remain distinguishable |

Add ambiguity, foreign scope, revoked access, missing observations, stale
context, tool-order permutations, and plausible distractor tools. Human-review
all paraphrases for changes to entities, quantities, negation, and time.
Portuguese/English variants are useful if both are intended application languages.

For Lua candidates, test frame sequences covering competition, cancellation,
expiration, and delayed feedback. Matching final brightness alone can hide
different persistent intents and lifetimes. Compare each candidate against
host-owned assertions. Keep the existing proposed repeated-run trace comparison
strict about source differences; add separate task-outcome assertions rather
than weakening normalization to make different programs appear identical.

Finite scenario agreement is evidence, not proof of general program equivalence.
Approval remains bound to exact source and context; passing equivalent scenarios
must not transfer approval to different source.

## Recommended integration sequence

- Keep L0 focused on accurate capability metadata and executable authority.
- Use L2/L3 deterministic fixtures to define expected semantic outcomes.
- At the MCP milestone, test native/MCP operation parity without a model.
- Once an external model integration is authorized, run reviewed paraphrase
  families against those fixtures with pinned model, prompt, tool descriptions,
  permissions, inputs, and budgets. Repeat attempts to separate wording effects
  from sampling variability.

Report per-variant correctness, the fraction of families where every variant
succeeds, meaning-changing control failures, unauthorized actions, unnecessary
calls, and incomplete runs. Do not use consistency alone as a pass condition.

The smallest useful addition is an explicit evaluation requirement: equivalent
requests preserve verified outcomes and authority under equivalent context,
while meaning-changing requests remain distinguishable. No new runtime, behavior
IR, model provider, or semantic-trigger engine is needed to state this requirement.
