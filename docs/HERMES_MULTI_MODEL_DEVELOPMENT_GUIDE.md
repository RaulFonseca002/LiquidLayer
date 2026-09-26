# Liquid implementation and review workflow

**Revision:** 6 September 2026. This file keeps its historical filename so
existing links remain valid. It replaces the earlier Hermes/Fable/Sol role
allocation. The owner selected **Claude implements, Codex independently reviews,
owner approves advancement**. Hermes is optional coordination tooling.

## Authority and prerequisites

Use the shared authority rule in [AGENTS.md](../AGENTS.md). Tracking identifies
the activated step. Read the [roadmap](LIQUID_STAGE2_PLAN.md),
[common contract](LIQUID_IMPLEMENTATION_CONTRACT.md), active spec and cited
Solid code/tests. Model preference, a passing test or an orchestrator status
cannot override owner scope or imply approval.

No Stage 2 implementation step is active; the Solid hardening and the L0–L6
design documents are already on `main`. Do not treat these implementation
instructions as activation of L0. Each implementation step starts from the
exact recorded `origin/main` base after the owner activates it. Use a
short-lived branch/worktree and preserve other unfinished work.

## The step loop

1. **Freeze the packet.** Record the step ID, base/spec SHA, outcome, allowed
   files, prerequisites, non-goals, test oracles and commands. The owner activates
   this bounded step, not every remaining milestone.
2. **Claude proves the failure.** Add the required behavioral test and record
   the pre-implementation result. A missing API can first fail compilation;
   after it compiles, verify the behavioral oracle also rejects a stub/pre-fix
   implementation in a disposable checkout.
3. **Claude implements and verifies.** Include substantive `.cpp` logic within
   the activated scope. Run focused tests, strict full tests and applicable
   milestone checks. Supply the diff and exact results, including failures.
4. **Codex reviews independently.** Start from the frozen packet, specification,
   code diff and machine evidence. Check behavior, authority, failure cases,
   test quality, packaging and documentation consistency. Inspect cited code;
   do not approve from Claude's narrative alone.
5. **Correct and recheck.** Claude addresses each finding with evidence. Codex
   checks the changed outcome and adjacent regressions. After two correction
   rounds on the same unresolved issue, present the disagreement/evidence to
   the owner rather than continue a blind retry loop.
6. **Owner gate.** Record review closure, checks and the owner's decision.
   Only then activate the next step. Milestone completion additionally requires
   its full sanitizer/package/coverage gates and reconciliation of its docs.

No worker self-approves. `No blocking findings` is a reviewer judgment, not an
owner signature or proof that all defects are absent. An unavailable model or
test environment remains unavailable; do not fabricate a substitute review.

## Task packet

Use one packet per step. Templates belong in the task handoff or completion
record; no new orchestration system is required.

~~~~markdown
# Task: Lx.y — observable outcome

Base: exact repository commit
Specification: exact revision and section
Activation: owner's recorded instruction for this step
Prerequisites: accepted prior steps and required baseline fixes

Outcome:
- Observable behavior the step must deliver

Allowed files:
- Exact subset of the milestone allowlist

Non-goals:
- Adjacent behaviors/files that are not part of this step

Acceptance:
- Input/action -> expected result, including failure cases

Verification:
```sh
exact focused commands
exact strict/full and applicable gate commands
```

Handoff:
- Diff/base identity and changed files
- Red/green evidence and full-suite results
- Unverified checks, failures, and any contract disagreement
~~~~

Never leave a consequential API, cleanup policy or error outcome as "implementer
chooses" in an activated packet. The spec supplies those decisions. If feasibility
evidence invalidates one, record the issue, correct the spec and obtain any needed
scope activation before implementing the replacement decision.

## Review and completion record

For each finding record severity, file/symbol, violated contract, concrete
trigger, expected versus actual behavior, and required evidence. Distinguish
confirmed defect, missing evidence and optional improvement. Do not turn style
preferences into invented correctness requirements.

For closure record the fix reference, test that detects the defect, pre-fix
failure and post-fix result. Reviewer verdict is one of `blocking findings`,
`missing evidence`, or `no blocking findings`. The owner decision is a separate
field with a real instruction/reference; never pre-populate it.

Update tracking after the gate with exact step/base/spec identity and results.
Keep implementation-ready, implemented, tested, reviewed and owner-accepted as
different states. A documentation pass can satisfy only documentation evidence.

## Optional Hermes coordination

Hermes may pass frozen packets to Claude and return the diff/evidence to Codex
when the owner authorizes that workflow. It does not select architecture,
change role permissions, self-review, broaden files, or approve progression.
The same packet and review artifacts must remain usable without Hermes.

This document no longer prescribes model versions, subscription/auth setup,
CLI flags, custom skill installation, Kanban infrastructure, or a speculative
orchestrator implementation. Those are separate environment tasks, not Liquid
prerequisites. Do not run model CLIs or spawn workers merely because this guide
describes the future handoff; require authorization in the active task.

## Publication and evidence limits

Implementation does not automatically authorize commits, pushes, PRs, merges
or releases. Keep the result reviewable in the task worktree and follow the
owner's publication instruction. Never force-push/rebase published main.

Tests demonstrate their asserted cases. Independent review adds scrutiny, not
certainty. Record remote CI only when its actual run was checked; local success
is not remote success. Simulation demonstrates mechanics, not user suitability
or physical device completion.

## Appendix — Historical workflow notes (29 August 2026, non-normative)

These notes are preserved from the pre-hardening guide for reference only and
do not override [AGENTS.md](../AGENTS.md) or the normative sections of this guide.

### Preflight

Before any write task, confirm and record: the repository and worktree; the
current branch/ref and base SHA; a clean base, or a deliberately dirty base
whose pre-existing changes are listed and left untouched; the activated step
ID; `AGENTS.md` and the step spec have been read; and the authority the task
actually grants. Never start writing from an unknown dirty checkout.

### Conflict report

When implementer and reviewer still disagree after evidence has been gathered,
hand the owner a short conflict report instead of choosing silently:

```text
question under dispute
each position, stated neutrally
direct specification/code evidence
reproduction or test evidence
smallest consequence of each choice
the decision the owner is being asked to make
```

### Evaluating the workflow

Judge the role split from observed Liquid work, not generic benchmark
reputation. For selected steps, record per worker: harness and effort; success
or failure; owner corrections required; accepted and false findings; scope
drift; changed-line count; focused and full-suite results; time/turns; token or
usage cost when available; defects caught by the other side; and retries.
Change one variable (model, prompt, effort or harness) at a time when trying to
explain a difference. Useful probes include the separate read/write schema
requirement, Integer-versus-Number non-coercion, the empty-array authoring
limit, unusual access-path escaping, and a review diff seeded with plausible
but out-of-scope features.
