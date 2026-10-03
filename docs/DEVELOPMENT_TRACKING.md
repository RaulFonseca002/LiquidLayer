# Liquid Development Tracking

## Current status

**Solid:** v0.1.0 released; the pre-Liquid hardening is landed on `main`
(`5979e14`, merged in `3e202ef`; follow-up `2aee8a4`). See
[Pre-Liquid Hardening — September 2026](#pre-liquid-hardening--september-2026).
**Liquid documentation:** L0–L6 specified and integrated on `main`.
**Implementation activation:** L0 activated by the owner on 29 September 2026
(see the [L0 activation record](#l0-activation-record)); implemented on
`feat/liquid-l0`, Codex review passed, and approved by the owner on 29 September
2026. L1 activated by the owner on 30 September 2026 (Discord `#liquid-layer`,
Hermes run `tcc-l1-2026-09-30`; see the [L1 activation record](#l1-activation-record));
implemented on `feat/liquid-l1`, Codex review passed, and approved by the owner
on 30 September 2026. No step is active now. L2–L6 remain unimplemented and
inactive until the owner activates the next one.

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
| [L1](LIQUID_L1_IMPLEMENTATION_SPEC.md) | Host-selected scope and immutable proposals | L0 | Done; owner-approved 30 September 2026 |
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
| Remote CI | Passed: GitHub Actions run `36634818550` on `6202d93`, 29 September 2026, 6/6 jobs |
| Codex review | Round 1 NEEDS_CHANGES (B1 schema-owned bytes, B2 status wording); round 2 NEEDS_CHANGES (B3 value accounting, B4 whole-manifest fields); round 3 PASS, no blocking findings, all R1–R6 and AC-1–AC-11 pass |
| Owner decision | Approved 29 September 2026 (Discord), with authorization to commit and merge |

The GREEN and sanitizer evidence above was reproduced by Hermes; Codex reviewed
the diff and evidence independently (read-only).

### L1 activation record

| Field | Value |
| --- | --- |
| Step | L1 — Prospective scope and immutable proposals (L1.1–L1.4) |
| Owner activation | 30 September 2026, via Discord `#liquid-layer` (Hermes run `tcc-l1-2026-09-30`) |
| Branch | `feat/liquid-l1` (worktree `.worktrees/liquid-l1`) |
| Base SHA | `6202d9366a6bc8eff01a65d50043071ebb3e2625` |
| Specification revision | [LIQUID_L1_IMPLEMENTATION_SPEC.md](LIQUID_L1_IMPLEMENTATION_SPEC.md) at the base SHA (last changed in `fa45be7`) |
| Prerequisites | L0 done and owner-approved 29 September 2026 |
| Implementer / reviewer / approver | Claude / Codex / owner (Hermes orchestrates) |

Allowed-file subset. New: `include/liquid/authoring/Types.hpp`,
`include/liquid/authoring/AuthoringSession.hpp`,
`src/authoring/AuthoringSession.cpp`, `tests/test_authoring_scope.cpp`,
`tests/test_authoring_proposal.cpp`, `tests/test_authoring_package.cmake`,
`examples/installed-package/authoring.cpp`. Existing:
`include/liquid/scripting/LuaBehaviorRunner.hpp`,
`include/liquid/scripting/LuaCapabilityManifest.hpp`,
`src/scripting/LuaBehaviorRunner.cpp`, `src/scripting/LuaCapabilityManifest.cpp`,
`tests/test_lua_manifest.cpp`, `tests/test_lua_behavior.cpp`, `CMakeLists.txt`,
`cmake/LiquidConfig.cmake.in`, `cmake/README.md`,
`examples/installed-package/CMakeLists.txt`, `examples/consumer_targets.cmake`,
`examples/source-tree/CMakeLists.txt`, this file, `AGENTS.md` (Current work
only), `docs/PUBLIC_API.md`, `docs/TRACEABILITY.md`, `CHANGELOG.md`
(Unreleased), `README.md` (component/status rows only).
Forbidden: World, Runtime, events/effects, `include/liquid/detail/`, Simulation
headers and sources, `apps/`, `third_party/`, `.github/`, `ARCHITECTURE.md`, the
L0–L6 spec texts and the common contract, other worktrees, new dependencies,
version bumps.

### L1 decisions — Hermes adjudications, 30 September 2026

Binding clarifications of the spec recorded for this step (not owner approval):

1. A1: the `tests/test_authoring_package.cmake` driver is allowed for the
   `authoring_package` CTest.
2. A2: `Liquid::Authoring` and the three `authoring_*` CTests are this step's
   targets.
3. D1: typed submission and grant structs make unknown fields unrepresentable;
   a test that builds them field by field is the evidence. Duplicate
   (type, component) grants fail the whole scope. Half a managed pair is
   `InvalidInput`; a full pair is `NotFound`.
4. D2: script-control components are rejected by the session after the runner
   capture, as `InvalidInput` naming the grant, and nothing is stored. That
   successful capture freezes runner registration; a failed capture does not.
   Both are tested.
5. D3: discover and submit first resolve target identities through
   `scope_targets`, with no value read; a missing, removed or recreated target
   is `StaleScope`. Only then do they re-capture through `scope_manifest`;
   value growth past L0 limits is `LimitExceeded`; any other capture failure
   is `HostError`.
6. D4: `LuaManifestErrorCode::InvalidGrant` is an additive L0 change; existing
   L0 tests stay unchanged.
7. D5: a third private UTF-8 validator is allowed, marked `shortcut:` with its
   reason.
8. D6: the `authoring_package` driver forwards compiler and sanitizer flags.
9. D7: a revoked scope is erased; later discover, submit, replace and revoke
   return `NotFound`. This is documented and tested.
10. D8: Authoring tests use public registration and are wired outside the
    legacy test loop.
11. The generic header install excludes `include/liquid/authoring/`, so a
    default package ships no Authoring headers.
12. The `AuthoringSession` constructor may throw `std::invalid_argument` for
    configuration errors; every other public call returns `AuthoringResult`.
13. Design adjustment after review B4: the design's "no separate liveness API"
    is replaced by the host-only `scope_targets`. No new module or dependency.

### L1 completion record

| Evidence | Result |
| --- | --- |
| RED (stubs, strict GCC build 0 warnings) | `authoring_scope` 4/28 cases passed, `authoring_proposal` 0/15, `authoring_package` failed at the consumer run; the pre-existing 32/32 passed |
| GREEN focused | `ctest --test-dir build/strict -R '^authoring_(scope\|proposal\|package)$' --output-on-failure`: 3/3 pass; `authoring_scope` 28/28 (518 assertions), `authoring_proposal` 15/15 (434 assertions) |
| Correction cycle 1 (review B1–B5) | Seven new review cases; Hermes ran the reviewer's repros first on the pre-fix code and they failed. B1: the script type is validated through the World typed check; B2: the manifest budget is the exact L0 logical size; B3: one checked issuer, no ID or revision wraps; B4: target identity is checked before value capture; B5: name bounds come before any copy or encoder call. After the fix: `authoring_scope` 33/33 (589), `authoring_proposal` 17/17 (484) |
| Strict GCC suite (Authoring ON) | `ctest --test-dir build/strict --output-on-failure`: 35/35, 0 warnings |
| Default (Authoring OFF) | 32/32, no authoring tests registered; Core-only 18/18 |
| Consumers | Installed 4/4 (with `COMPONENTS Authoring`); source-tree 3/3; `LIQUID_BUILD_AUTHORING=ON` without Simulation fails at configure |
| L0 regression | `lua_schema` 19/19 (511), `lua_manifest` 32/32 (470), both unchanged |
| Gates | G8–G13: 0 violations, 0 include/target cycles; no Lua/Simulation/Core header includes Authoring; `git diff --check` clean |
| GCC sanitizers (Authoring ON) | ASan/UBSan 35/35, 0 errors; TSan 35/35, 0 warnings |
| Clang, coverage | Unverified locally (owner rule: GCC only, no tool installs) |
| Remote CI | Pending |
| Codex review | Round 1 NEEDS_CHANGES (B1–B5 code, B6 evidence record); round 2 PASS, 0 blocking findings. Non-blocking: DOC-1 (fixed in the docs round) and the design adjustment (decision 13) |
| Owner decision | Approved 30 September 2026 (Discord), with authorization to commit, merge and push |

The GREEN and sanitizer evidence was reproduced by Hermes on the final code;
Codex reviewed the diff and evidence independently (read-only) and ran no
tests.

### L1 correction record — session payload ceiling, 3 October 2026

| Field | Record |
| --- | --- |
| Order | Owner answer 5, 3 October 2026 (Discord coordinator thread 1556032278268878931, message 1556051740317589657); Hermes run `liquid-l1fix-2026-10-03` |
| Finding | F-L4-04: L1 did not enforce the contract's 64 MiB ceiling on all session-owned payloads; `AuthoringLimits` had no aggregate field and the session kept no byte total |
| Base | `f6f4af5`, branch `fix/liquid-l1-session-ceiling` |
| Change | `AuthoringLimits::maxSessionPayloadBytes = 64u << 20` (0 or above the default is rejected; a host may lower it) and one `AuthoringSession::Impl::sessionPayloadBytes` counter. `create_scope`, `replace_scope` and `submit` compute the new charge before any mutation and fail with `LimitExceeded`, leaving no partial state, when the ceiling would be passed. These checks run after the existing capacity and ID checks, so a rejected call consumes no ID, revision or session time. `revoke_scope` releases the scope's charge; `replace_scope` swaps the old charge for the new one; proposals, which L1 never removes, keep their charge after a scope revoke. The counter always equals the sum of the retained records' charges |
| Charge rule | Contract logical accounting (8 per scalar, ID or enum; 1 per boolean or optional flag; string bytes; 8 per element or field). Scope record: its fields, owner, grants, targets, private capture, plus the admitted manifest's L0 `logical_bytes()`. Proposal record: its fields, contract version, source, rationale, owner and the scope's capture counted again. Additions saturate, and a saturated charge is never admitted |
| Reuse | L2 evaluation reservations and L4 journal storage charge the same `AuthoringLimits::maxSessionPayloadBytes` field and the same single `Impl` counter; no second counter or budget class |
| Tests | New `authoring_session_ceiling` (`[l1fix]`, 9 cases). RED (enforcement stubbed): 3/8 pass, 5 fail on assertions, pre-existing 35/35. GREEN: 9/9; `authoring_scope` 33/33 (589) and `authoring_proposal` 17/17 (484) unchanged; strict GCC suite 36/36, 0 warnings |
| Known shortcut | The 8/1/8 accounting constants are a second private copy of the ones in `src/scripting/LuaCapabilityManifest.cpp`, which was outside this step's allowlist (marked `shortcut:` in code) |
| Found by | L3/L4 preparation lane, spec finding F-L4-04 (L4 spec assumed the ceiling already existed in L1) |
| Hardening | Owner chose option B on 3 October 2026 (message 1556064701161410590): one extra round for review note NB-1. `replace_scope` now builds every allocating value (capture and target copies, returned view) before it touches the record or the counter, and commits only with non-throwing moves and scalar assignments, guarded by `static_assert`s. No runtime test: forcing `bad_alloc` there needs a fault-injecting `operator new`, which is new test infrastructure |
| Hermes gate (final code) | GCC only. Strict (Authoring ON, warnings as errors) 36/36, 0 warnings; `authoring_session_ceiling` 9 cases, 345 assertions; default 32/32; Core-only 18/18; installed and source-tree consumers 3/3 each; ASan/UBSan (GCC, Authoring ON) 36/36 with 0 sanitizer reports; design gates G8a–G13 equal to baseline, no target cycles, no tracked binaries, `ARCHITECTURE.md` unchanged, no pre-existing or RED test line removed. Not run: TSan, Clang, coverage (Clang/gcovr not installed) |
| Review | Codex (gpt-6.1-sol) was refused by the gateway, so a fresh read-only Claude Opus reviewer ran both rounds without running tests. Round 1 (full change): PASS, 0 blocking, 6 non-blocking. Round 2 (NB-1 hardening only): PASS, 0 blocking, 2 non-blocking |
| Review notes and status | NB-1 `replace_scope` counter/record drift on `bad_alloc`: **fixed** (hardening round). NB-2 the `sizeof` `static_assert` guard on the runner limits cannot see a new `bool` that fits in padding (pattern copied from L0 `LuaCapabilityManifest.cpp`): **open follow-up**, L0 and L1 together. NB-3 `BehaviorProposal::session` (32-hex-digit id) is charged as an 8-byte ID per the contract: **accepted; rule for L2 evaluation and L4 journal accounting**, which must charge session ids the same way. NB-4 a submission that is both over the ceiling and stale now returns `LimitExceeded` before `StaleScope`, matching the existing capacity-before-recapture order: **accepted**. NB-5 `submit` complexity +1 branch (about 12, was about 11): **accepted**. NB-6 the test helper assumes one target and one capture entry per grant, anchored by the explicit 481-byte check: **accepted**. R2-1 `return view;` relies on C++20 implicit move: **accepted as is** (owner declined a style round). R2-2 `create_scope` copies the manifest for its return value after the commit, so a `bad_alloc` there leaves a stored, correctly charged scope whose id the caller never receives (base L1, counter stays consistent): **open follow-up** |
| Owner decision | Approved 3 October 2026 (Discord coordinator thread 1556032278268878931, message 1556079628156797090), with authorization to commit on `fix/liquid-l1-session-ceiling` only; no merge or push |

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
