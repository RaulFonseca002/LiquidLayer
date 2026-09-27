# Liquid Development Tracking

## Current status

**Solid:** v0.1.0 released; the pre-Liquid hardening is landed on `main`
(`5979e14`, merged in `3e202ef`; follow-up `2aee8a4`). See
[Pre-Liquid Hardening — September 2026](#pre-liquid-hardening--september-2026).
**Liquid documentation:** L0–L6 specified and integrated on `main`.
**Implementation activation:** none. L0–L6 code remains unimplemented, and no
Stage 2 step is active until the owner records its activation below.

On 6 September 2026 the owner authorized a complete documentation review:
specify every retained milestone; allow evidence-backed Solid change proposals;
use Claude implementation, Codex review, and owner-approved advancement.
Preparing these documents was not acceptance of the resulting design or
activation of L0. See the [validation report](history/LIQUID_DOC_VALIDATION.md).

On 10 September the owner requested branch cleanup and a commit of the finished
documentation work. The semantic-invariance research from `8003ee6` is retained
and uses the same milestone numbering.

Owner decisions recorded 26 September 2026:

1. L0 is not active. No Stage 2 step is active until the owner separately
   activates one.
2. For an owner-activated step, Claude implements and tests the whole allowed
   scope, including substantive `.cpp` work; Codex independently reviews; the
   owner approves advancement and keeps publication/merge authority. This
   replaces the earlier owner-only `.cpp` convention.
3. The revised L0–L6 order and specifications, including L4–L6 and the L6
   dependency pins (Python MCP 2.1.1, nlohmann/json 3.12.0), are accepted as
   specified-but-inactive design.

## Solid foundation

M1–M6 and S0–S7 completed at v0.1.0 on 22 August 2026. Evidence in
[COMPLETE_SOLID.md](history/SOLID_V01_COMPLETION.md) and [TRACEABILITY.md](TRACEABILITY.md)
is dated, not a permanent assertion that later reviews cannot find defects.

| Baseline | Revision | Meaning |
| --- | --- | --- |
| Refreshed `origin/main`, 10 September | `af5af08befea` | Design-review baseline; ancestor of the documentation branch |
| Unmerged documentation, 10 September | `76f00e414445` | Starting content of the documentation review |
| Committed Solid hardening | `5979e149e042` | Code foundation reviewed by the documentation pass |
| Hardening merge on `main` | `3e202ef` | `fix/pre-liquid-hardening` merged, including `2aee8a4` |
| Blind Core coverage tests | `f3ef421` | 25 contract-level tests; Core line coverage 93.49% (gcovr 8.3); strict suite 30/30 |

The dated design-review evidence is pinned in the validation report; its
post-handoff reconciliation covers `8003ee6`→`f3ef421`. An implementation step
records its own exact base SHA when the owner activates it.

## Pre-Liquid Hardening — September 2026

**Status:** Done, merged to `main` in `3e202ef` (follow-up `2aee8a4`); not a milestone approval and does not advance L0.
B1–B6, C1, C3, C4, D1 and D2 closed with regressions that fail on the reviewed code; C2 remains a documented finite-session limitation; C5 is L0 fixture guidance.
Superseded by [history/PRE_LIQUID_REVIEW.md § Hardening closure record](history/PRE_LIQUID_REVIEW.md#hardening-closure-record), which keeps this section's former text (closing commits, regression names, verification) verbatim.

## Stage 2 sequence

The [roadmap](LIQUID_STAGE2_PLAN.md) owns rationale; the
[common contract](LIQUID_IMPLEMENTATION_CONTRACT.md) owns shared APIs,
limits and gates; each spec owns its milestone's additions and test steps.

| Milestone | Deliverable | Dependencies | Status |
| --- | --- | --- | --- |
| [L0](LIQUID_L0_IMPLEMENTATION_SPEC.md) | Lua schemas and capability manifests | Reconciled Solid baseline | Specified; inactive |
| [L1](LIQUID_L1_IMPLEMENTATION_SPEC.md) | Host-selected scope and immutable proposals | L0 | Specified; inactive |
| [L2](LIQUID_L2_IMPLEMENTATION_SPEC.md) | Isolated lifecycle evaluation | L1 | Specified; inactive |
| [L3](LIQUID_L3_IMPLEMENTATION_SPEC.md) | Scoped current truth and frame evidence | L2 | Specified; inactive |
| [L4](LIQUID_L4_IMPLEMENTATION_SPEC.md) | Bounded session journal | L3 | Specified; inactive |
| [L5](LIQUID_L5_IMPLEMENTATION_SPEC.md) | Approval, activation, replacement and stop | L4 | Specified; inactive |
| [L6](LIQUID_L6_IMPLEMENTATION_SPEC.md) | Optional local MCP adapter | L5 | Specified; inactive |

The old provisional L4=MCP and L6=evidence IDs are superseded. Evidence now
precedes operations; transport follows the complete semantic lifecycle.

## Activation and completion records

Record the step ID, exact base SHA, specification revision, allowed-file subset,
prerequisites, and owner activation before implementation. Claude implements
and tests, Codex reviews, then the owner approves progression.

Completion records contain focused red/green evidence, strict-suite commands
and results, review findings and closure, relevant sanitizer/package evidence,
and the owner decision. Never pre-fill approvals, test totals or remote CI.
No step has been activated, and no implementation steps have completion
records yet.

## Liquid Layer

Future application work owns model routing, user/context meaning, privacy and
consent, real hardware and human studies. Stage 2 proves deterministic
mechanics and a local transport boundary, not clinical benefit, remote-service
security, or indefinitely durable adaptive history.

Current owner rule on human data collection (confirmed 26 September 2026;
source: [history/STATE_EVALUATION_PT.md](history/STATE_EVALUATION_PT.md) § 14,
decision 12, line 881 at `bf56159`):

> 12. Nenhuma coleta humana começa antes de protocolo, CEP, plano de dados, avaliação LGPD documentada e RIPD quando o tratamento for de alto risco ou aplicável.

English rendering: no human data collection begins before there is a protocol,
research ethics committee (CEP) approval, a data plan, a documented LGPD
(Brazilian General Data Protection Law) assessment, and a data protection
impact report (RIPD) when the processing is high-risk or otherwise applicable.
