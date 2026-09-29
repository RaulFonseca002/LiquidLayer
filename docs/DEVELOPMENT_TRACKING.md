# Liquid Development Tracking

## Current status

**Solid:** v0.1.0 released; the pre-Liquid hardening is landed on `main`
(`5979e14`, merged in `3e202ef`; follow-up `2aee8a4`). See
[Pre-Liquid Hardening — September 2026](#pre-liquid-hardening--september-2026).
**Liquid documentation:** L0–L6 specified and integrated on `main`.
**Implementation activation:** L0 activated by the owner on 29 September 2026
(see the [L0 activation record](#l0-activation-record)); implemented on
`feat/liquid-l0`, Codex review passed, and approved by the owner on 29 September
2026. No step is active now. L1–L6 remain unimplemented and inactive until the
owner activates the next one.

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
| [L0](LIQUID_L0_IMPLEMENTATION_SPEC.md) | Lua schemas and capability manifests | Reconciled Solid baseline | Done; owner-approved 29 September 2026 |
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

### L0 activation record

| Field | Value |
| --- | --- |
| Step | L0 — Lua schemas and capability manifests (L0.1–L0.4) |
| Owner activation | 29 September 2026, via Discord (Hermes run `tcc-l0-2026-09-29`) |
| Branch | `feat/liquid-l0` (worktree `.worktrees/liquid-l0`) |
| Base SHA | `b308b488657cb6f41bdbe34878dc8502b5576661` |
| Specification revision | [LIQUID_L0_IMPLEMENTATION_SPEC.md](LIQUID_L0_IMPLEMENTATION_SPEC.md) at the base SHA |
| Prerequisites | Reconciled Solid baseline on `main`; no earlier Stage 2 step |
| Implementer / reviewer / approver | Claude / Codex / owner |

Allowed-file subset. New: `include/liquid/scripting/LuaValueSchema.hpp`,
`include/liquid/scripting/LuaCapabilityManifest.hpp`,
`src/scripting/LuaValueSchema.cpp`, `src/scripting/LuaCapabilityManifest.cpp`,
`tests/test_lua_schema.cpp`, `tests/test_lua_manifest.cpp`, `ARCHITECTURE.md`.
Existing: `include/liquid/scripting/LuaBehaviorRunner.hpp`,
`src/scripting/LuaBehaviorRunner.cpp`, `tests/test_lua_behavior.cpp`,
`tests/test_lua_lifecycle.cpp`, `CMakeLists.txt`,
`examples/lua_consumer/main.cpp`, this file, `AGENTS.md` (Current work only),
`docs/PUBLIC_API.md`, `docs/TRACEABILITY.md`, `CHANGELOG.md` (Unreleased).
Forbidden: World, Runtime, events/effects, `third_party/`, `.github/`, `cmake/`,
the L0–L6 spec texts, new dependencies, new library targets, version bumps.

### L0 decisions — Hermes adjudications, 29 September 2026

Binding clarifications of the spec recorded for this step (not owner approval):

1. "No new CMake target" means no new library or installed target; the
   `lua_schema` and `lua_manifest` test executables are allowed.
2. A write-schema failure in a proposal is `InvalidProposal`; a read-schema
   failure on a readable snapshot during a run is `HostError`.
3. `Range` is a value outside declared bounds or not in the enum; `Limit` is a
   value over a fixed L0 ceiling.
4. A manifest `now` outside the Lua integer range is `HostError`.
5. Any `Binding::snapshot` failure, including a codec encode exception, is
   `SnapshotUnavailable`; any other host exception is `HostError`.
6. `LuaValueSchema.hpp` forward-declares `LuaValue`; callers of
   `capability_manifest` include `LuaCapabilityManifest.hpp`, as the example does.
7. New L0 diagnostics end in `...` when truncated; legacy diagnostics stay
   byte-identical.
8. `LuaModelBindingMetadata` is a validating class that cannot be constructed
   without both schemas; `symmetric_metadata` exists.

### L0 completion record

| Evidence | Result |
| --- | --- |
| RED (stubs, strict GCC build 0 warnings) | `lua_schema` 1/19 cases passed, `lua_manifest` 4/30 passed; failures were behavioral assertions |
| GREEN focused | `ctest --test-dir build/strict -R '^lua_(schema\|manifest)$' --output-on-failure`: `lua_schema` 19/19 (511 assertions), `lua_manifest` 30/30 (452 assertions) |
| Correction cycle 1 (review B1) | Schema-owned bytes now count toward the 1 MiB logical limit. The new `L0.4` boundary case failed first (1 MiB + 1 accepted), then passed; `lua_schema` 19/19 (511), `lua_manifest` 31/31 (463) |
| Correction cycle 2 (review B3, B4) | Read values follow the contract logical accounting, and the fixed manifest and per-capability fields are charged. The new numeric-value case and the rewritten exact 1 MiB case failed first, then passed; `lua_manifest` 32/32 (470) |
| Strict GCC suite | `ctest --test-dir build/strict --output-on-failure`: 32/32, 0 warnings |
| Consumers | Core-only 18/18; installed and source-tree consumers 3/3 each |
| Gates | G8–G13: 0 violations, 0 include cycles; `git diff --check` clean |
| GCC sanitizers | ASan/UBSan (`build/asan`, `-DLIQUID_ENABLE_SANITIZERS=ON`): 32/32, 0 sanitizer errors; TSan (`build/tsan`, `-DLIQUID_ENABLE_THREAD_SANITIZER=ON`): 32/32, 0 warnings |
| Clang, coverage | Unverified locally (owner rule: GCC only, no tool installs; Clang/gcovr not installed) |
| Remote CI | Pending |
| Codex review | Round 1 NEEDS_CHANGES (B1 schema-owned bytes, B2 status wording); round 2 NEEDS_CHANGES (B3 value accounting, B4 whole-manifest fields); round 3 PASS, no blocking findings, all R1–R6 and AC-1–AC-11 pass |
| Owner decision | Approved 29 September 2026 (Discord), with authorization to commit and merge |

The GREEN and sanitizer evidence above was reproduced by Hermes; Codex reviewed
the diff and evidence independently (read-only).

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
