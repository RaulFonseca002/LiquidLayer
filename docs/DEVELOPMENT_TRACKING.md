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
on 30 September 2026. L2 activated by the owner on 3 October 2026 (see the
[L2 activation record](#l2-activation-record)); implemented on
`feat/liquid-l2` and approved by the owner on 5 October 2026 (see the
[L2 completion evidence](#l2-completion-evidence--5-october-2026)); not yet merged into `main`. L3–L6
remain unimplemented and inactive until the owner activates the next one.

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
| [L2](LIQUID_L2_IMPLEMENTATION_SPEC.md) | Isolated lifecycle evaluation | L1 | Done; owner-approved 5 October 2026 (on `feat/liquid-l2`, not merged) |
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

### L2 activation record

| Field | Value |
| --- | --- |
| Step | L2 — Isolated deterministic proposal evaluation (L2.1–L2.4) |
| Owner activation | 3 October 2026, via Discord `#liquid-layer` (Hermes runs `liquid-l2-2026-10-03`, lane L2-A, and `liquid-l2-eval-2026-10-03`, lane L2-B). The owner asked for the safe steps in parallel threads and declined risky early L3/L4 work |
| Branches | `feat/liquid-l2` (worktree `.worktrees/liquid-l2`) and `feat/liquid-l2-eval` (worktree `.worktrees/liquid-l2-eval`); the second is merged into `feat/liquid-l2` before the combined diff is reviewed |
| Base SHA | `f6f4af56469fcf974c6424cd448b17bde424516f` |
| Specification revision | [LIQUID_L2_IMPLEMENTATION_SPEC.md](LIQUID_L2_IMPLEMENTATION_SPEC.md) at the base SHA |
| Prerequisites | L1 done and owner-approved 30 September 2026 |
| Implementer / reviewer / approver | Claude / Codex (fallback: a fresh read-only Claude reviewer while the gateway rejects Codex) / owner (Hermes orchestrates) |

Allowed-file subset (the spec's "Allowed files"). New:
`include/liquid/authoring/Evaluation.hpp`, `src/authoring/Evaluation.cpp`,
`src/authoring/BoundedEvaluationStore.hpp`, `tests/test_authoring_evaluation.cpp`.
Existing: Authoring session and types, the lifecycle header, implementation and
tests, the Authoring consumer, CMake, and affected docs.
Forbidden: World, Runtime, events/effects, `include/liquid/detail/`, Simulation
headers and sources, `apps/`, `third_party/`, `.github/`, `ARCHITECTURE.md`, the
L0–L6 spec texts and the common contract, other worktrees, new dependencies,
version bumps, generalizing or moving Scope's light-specific scenario.

### L2 decisions — Hermes adjudications, 3 October 2026

Binding clarifications of the spec recorded for this step (not owner approval):

1. A1: lane L2-A edits `tests/test_authoring_evaluation.cpp` and
   `examples/installed-package/authoring*.cpp` for L2.4 only after L2-B's review
   has passed and its work is merged into `feat/liquid-l2`; L2-B never edits
   them again.
2. A2: script selection and evaluation fixtures are not declared ARCHITECTURE
   axes. Selection is a `switch`; fixtures are a plain per-session map, with no
   registry or factory.
3. A3: the evaluator captures the isolated manifest itself through
   `runner->capability_manifest(...)` and does not trust the factory to return
   it.
4. A4: L2-B adds evaluation accounting (reservations and kept records) to the
   session payload ceiling. The missing L1 ceiling (`maxSessionPayloadBytes`,
   64 MiB, scopes and proposals) is an L1 defect; the owner decided on
   3 October 2026 (Discord `#liquid-layer`, Hermes record
   `liquid-next-2026-10-03/owner-answers.md`, answer 5) to fix it in its own
   lane (`fix/liquid-l1-session-ceiling`) before L2-B is merged. L2 reuses that
   field and its single session byte counter.
5. A5: a `SingleReadable` selection failure records no `ScriptExecuted` event
   and leaves the World unchanged.
6. A6: L2-B's `lighting-v1` factories use the legacy `"lifecycle"` overload;
   L2-A switches them to `LuaScriptSelection::SingleReadable` in L2.4. The
   evaluator does not inspect the mode.
7. A7: `normalized_trace` is built by L2-B in `Evaluation.cpp` and its
   equality is tested in L2.4. Schema identity is compared through manifest
   schemas plus binding metadata.
8. The asymmetry is intended: when no script is selected, the legacy named
   constructor leaves `last_result` as `nullptr`, while `SingleReadable` stores
   a non-null `HostError` with one of the three frozen diagnostics.
9. API-NOTE-1: the `SingleReadable` constructor also rejects an out-of-range
   `LuaScriptSelection` value with `std::invalid_argument` ("Lua lifecycle
   script selection is invalid"). This is an addition to the frozen design text
   and is tested.
10. BORROW-1 (optional hardening, not done in L2): `run()` passes a view of the
    script source in World storage to `execute_lifecycle`, as the code at the
    base SHA already did. It is safe while the script type cannot be proposed;
    passing the state's owned copy would remove the borrow.

### L2-A findings — 3 October 2026

Every finding of lane L2-A (design, L2.1, review, gates, tooling), per the
owner rule of 3 October 2026 ("everything we find must be documented for
later"). Status is fixed, accepted, or open follow-up.

| ID | Finding | Status |
| --- | --- | --- |
| L2A-F1 | Review TEST-GAP-1: the "script component is unavailable" diagnostic is reachable through public API (a stale or foreign script type handle makes `World::get_components` throw); the builder had claimed otherwise | Fixed: named `[l2.1]` test added, code unchanged |
| L2A-F2 | The `SingleReadable` constructor also rejects an out-of-range selection value | Accepted (decision 9), tested |
| L2A-F3 | BORROW-1: `run()` passes a view of World script storage to `execute_lifecycle` (as at the base SHA) | Open follow-up: optional hardening |
| L2A-F4 | A selection failure records no `ScriptExecuted` event, but the L2.1 fixture has no EventStore, so this is not asserted | Fixed in round 5 as L2A-F17: asserted through `normalized_trace` on the evaluation fixtures |
| L2A-F5 | The evaluator cannot check that a factory uses `SingleReadable` | Accepted (A6); covered by L2.4 tests |
| L2A-F6 | No public untyped World API gives a component's schema version; binding metadata has no schema name | Accepted (A7) |
| L2A-F7 | Legacy and `SingleReadable` report a missing script differently | Accepted (decision 8) |
| L2A-F8 | L1 never implemented the 64 MiB session payload ceiling | Fixed by the owner-approved L1-fix lane (decision 4) |
| L2A-F9 | Script selection and evaluation fixtures are not declared ARCHITECTURE axes | Accepted (A2) |
| L2A-F10 | Clang, gcovr coverage and the jscpd clone gate cannot run locally | Open follow-up: GCC-only owner rule; remote CI |
| L2A-F11 | The repository has no formatter or linter configuration (pre-existing) | Accepted |
| L2A-F12 | The Codex reviewer model is rejected by the gateway; a fresh read-only Claude reviewer was used | Open follow-up (tooling) |
| L2A-F13 | A relative prompt path made the first build round start no worker | Fixed (absolute paths) |
| L2A-F14 | The baseline build overlapped another lane's build | Accepted: separate build trees, only slower |
| L2A-F15 | One Hermes re-check timed out and was discarded | Accepted: evidence re-verified by diff hash |
| L2A-F16 | Combined review c1 N1: after the L2.4 switch to `SingleReadable`, the legacy `"lifecycle"` overload no longer drives the evaluator, the `!lastLifecycle` branch (`Evaluation.cpp:490`) looked untested, and the test at line 1444 was misnamed | Fixed in round 5. The premise was corrected: the line-1444 test (no script grant, so the candidate is not a lifecycle member) does hit the `!lastLifecycle` branch; it is renamed accurately, and legacy HostError (two sections), legacy Passed and a true `SingleReadable` selection-failure test were added |
| L2A-F17 | Combined review c1 N2 (same as L2A-F4): no assertion that a `SingleReadable` selection failure records no `ScriptExecuted` event, although the L2.4 fixtures have an EventStore | Fixed in round 5: asserted through `normalized_trace`; pointing the check at a passing evaluation fails (exit 42), and the restored test passes |
| L2A-F18 | Combined review c1 N3: `require_same_evaluation` skips session and proposal, which are equal for a same-session repeat | Fixed in round 5: a new appended test compares session and proposal for a same-session repeat |
| L2A-F19 | Combined review c1 N4: the tests at lines 1077 and 1859 compare a pointer into an already-destroyed isolated `Runtime` with `&lab.host.world` (implementation-defined; always true) | Open follow-up |
| L2A-F20 | Tooling: the first round-5 launch read a relative prompt path after `cd` | Fixed: `run_build.sh` uses `realpath` and checks that the prompt exists |
| L2A-F21 | Focused review c2: the renamed line-1444 test ("a SingleReadable candidate without a script grant has no lifecycle result and is HostError") did not pin its diagnostic | Fixed in round 7: it also checks that case 0's diagnostic contains "no lifecycle result for the candidate" |
| L2A-F22 | Focused review c2: the test helper `selection_factory` repeats about 25 lines of `lighting_factory` (test-helper duplication; the jscpd scan covers only `include/`, `src/` and `apps/`) | Open follow-up |
| L2A-F23 | Focused review c2: `CLAUDE.md` contradicted `AGENTS.md` and tracking on the L2 status | Fixed in round 7 |
| L2A-F24 | Focused review c2: the mutation summary in `TRACEABILITY.md` does not say which mutation broke the live-state check. It was the forced-legacy mutation (c) of round 4, which also fails the live-state case because that case evaluates the two-scripts fixture | Accepted: recorded here; `TRACEABILITY.md` was outside round 7's allowed files |
| L2A-F25 | Focused review c2: the L1 correction record gives RED as "3/8" while the step has 9 cases; the ninth case was added in GREEN (pre-existing L1-fix history) | Open follow-up (history wording); the L1 record is left unchanged |
| L2A-F26 | Focused review c3 NB1: the "effect address for ungranted T.c" diagnostic named a component outside the candidate's scope (L3 spec :124 forbids hidden names) | Fixed (r9): the diagnostic is "fixture mismatch: effect address for an ungranted component" |
| L2A-F27 | Focused review c3 NB2: one granted component could claim several (route, target) addresses, so a mis-declared factory could make a hidden target's counts appear | Fixed (r9): at most one address per granted component; a second is the `HostError` "fixture mismatch: effect address for T.c repeats its component". Open follow-up: derive addresses from a trusted Solid query (Runtime effect binding lookup) to remove host declarations |
| L2A-F28 | Focused review c3: competitor commands on a granted target still count in the frame summary | Accepted per owner wording (option A counts effects on granted targets); note for L3 (L3 spec :121-125) |
| L2A-F29 | Focused review c3: `PUBLIC_API.md` spliced the `grantedEffects`/`HostError` clause into the L2 type list | Fixed (r9) |
| L2A-F30 | Focused review c3: the `TRACEABILITY.md` L2.3 mutation sentence was not labelled as the builder's claim | Fixed (r9): labelled "Builder-reported" |
| L2A-F31 | Focused review c3: the `complete` wording in `PUBLIC_API.md` named only the budget and response-bound causes | Fixed (r9): described as structural incompleteness, which includes every budget truncation |
| L2A-F32 | Focused review c3: a historical reference to test line :1588 | Accepted (historical) |

Also recorded: the installed Authoring consumer's proposal source was changed
to an `on_start` hook (a builder deviation in L2.4); the combined review c1
accepted it.

### L2-B findings — 3 to 5 October 2026

Every finding of lane L2-B (L2.2 and L2.3, its reviews, gates and tooling), per
the same owner rule. Status is fixed, accepted, open follow-up, or open
awaiting an owner decision. Source: Hermes run `liquid-l2-eval-2026-10-03`
(`tracking-findings.md`, `fix-r1-report.md`, review verdicts r1 and r2).

| ID | Finding | Status |
| --- | --- | --- |
| L2B-1 | Builder deviation: an empty `EvaluationSuite` (no cases) is rejected with `InvalidInput` at `"cases"`; the design's error table does not list it, but an empty suite could only report a vacuous `Passed` | Accepted (L2-B reviews r1 and r2 raised no finding) |
| L2B-2 | Builder deviation: when the public result exceeds `maxEvaluationResponseBytes`, evidence is cleared first, then cases; the design gives only the outcome (`LimitExceeded`, `complete = false`) | Accepted (L2-B reviews r1 and r2 raised no finding) |
| L2B-3 | `normalized_trace` entries are built in `Evaluation.cpp` (A7), but the `AuthoringSession::normalized_trace` member lives in `AuthoringSession.cpp` because it needs `Impl` | Accepted |
| L2B-4 | Under a very small `maxEvaluationResponseBytes`, the record header alone (about 89 bytes plus the fixture name) can exceed the bound; IDs, status and error code are never truncated, so the returned logical size can exceed the configured bound in that edge | Open follow-up (neither L2-B review judged it) |
| L2B-5 | The evaluator cannot enforce that a host factory routes events through `runtimeOptions.eventStore`; a non-compliant factory escapes the store budget and the trace | Accepted (trust model: host factories are trusted native code) |
| L2B-6 | `normalized_trace` rewrites only unsigned integer fields under handle-like keys; handles embedded in strings (for example encoded keys) are not normalized | Open follow-up. The L2.4 two-run trace-equality checks pass on the shipped fixtures |
| L2B-7 | `shortcut:` `bounded_diagnostic` in `Evaluation.cpp` duplicates the UTF-8-safe truncation in `make_error` (`AuthoringSession.cpp`); upgrade trigger: a third copy | Accepted (marked in code) |
| L2B-8 | Every RED assumption matched real Solid behavior; the test file was byte-identical between RED and GREEN (no assertion changed) | Fixed (evidence record; nothing to change) |
| L2B-9 | Tooling: the disk filled during the first Hermes gate and the GCC ASan/TSan builds died ("No space left on device"; 0 sanitizer findings before the failure) | Fixed: re-run with `-O0`, no `-g`, one at a time, build directories deleted after; ASan/UBSan 36/36 with 0 errors, TSan 36/36 with 0 warnings |
| L2B-10 | Tooling: the first RED launch used a relative prompt path after `run_build.sh` changed directory; no worker ran and no file changed | Fixed (`realpath`) |
| L2B-11 | Design A4 was superseded by owner answer 5: L2-B must reuse L1-fix's `maxSessionPayloadBytes` field and single `sessionPayloadBytes` counter instead of defining its own; both branches defined the same names | Fixed in the merged tree: one field (`Types.hpp:168`) and one counter (`AuthoringSession.cpp:344`) |
| L2B-12 | Lane split: L2-B's factories use the legacy `"lifecycle"` overload; the `SingleReadable` switch, the two-behavior different-slot case and two-run trace equality moved to L2-A's L2.4 | Accepted (A1, A6) |
| L2B-13 | L2-B review r1, blocking (Q17/P11): a third private copy of the accounting constants (`Evaluation.cpp`) and a duplicate saturating add (`BoundedEvaluationStore.hpp` and `AuthoringSession.cpp`); the L1 upgrade trigger was met | Fixed in fix-r1: one definition in `src/authoring/PayloadAccounting.hpp`; review r2 PASS. The L0 copy in `LuaCapabilityManifest.cpp` stays, marked `shortcut:`, outside the L2 allowlist |
| L2B-14 | Scoped summaries: `summarize()` (`Evaluation.cpp`) counts every command, report and observation of the isolated frame, including ones on targets outside the candidate's scope. The spec asks for scoped summaries in which hidden competitor details are never returned. The hidden test `[repro-scoped-summary]` fails: frame 0 has 2 commands (the candidate's office 70 and a competitor's hallway 90) and frames 0–1 have 2 report statuses (deferred feedback), where the test expects 1 each | Fixed (owner decision row 10, 5 Oct 2026: option A + host-only whole-world totals). The owner said: "lets go with A and the flag for whole world then, i think it is important to keep the options open".<br>- Summary counts now include only effects on targets bound to a granted component. Observations also need a readable grant. Case factories declare those targets in `PreparedEvaluation::grantedEffects`.<br>- The trusted-host-only `AuthoringSession::world_totals` returns the whole-world counts and a `complete` flag. It derives them from the kept trace, so nothing new is stored |
| L2B-15 | L2-B review r1: no test covers the partial reservation release | Fixed in fix-r1: "L2.3 oracle: a kept evaluation releases the unused part of its reservation"; removing or reversing the release makes it fail (2 assertions) |
| L2B-16 | L2-B review r1 (cohesion note): the evaluator's internal interface is declared in `BoundedEvaluationStore.hpp` | Accepted (follow-up note) |
| L2B-17 | L2-B review r1: the `EvaluationTicket` comment said "copied at admission", but the suite is referenced | Fixed in fix-r1 (comment only) |
| L2B-18 | L2-B review r1: if `run_evaluation` throws, the record is `HostError` with zero cases instead of listing each case as `NotRun` | Open follow-up |
| L2B-19 | Process: a round-3b builder put its build in the background and exited | Fixed: foreground-only build rule |
| L2B-20 | L2-B review r2: `Evaluation.cpp` returns a literal `1` for a Boolean instead of `LogicalFlagBytes` (reservation unaffected) | Open follow-up |
| L2B-21 | L2-B review r2: the release test does not catch an over-release (`sessionPayloadBytes -= reservation`); the exact retained charge should be pinned later | Open follow-up |
| L2B-22 | L2-B review r2, tied to L2B-14: the hidden failing repro (`tests/test_authoring_evaluation.cpp:1588`) must become a passing regression or be removed after the owner decides | Fixed (owner decision row 10, 5 Oct 2026: option A + host-only whole-world totals). The repro is now the passing regression "L2 b14: frame summaries count only the candidate's granted targets" (`[l2-b14]`). Only its name and tag changed |

### L2 completion evidence — 5 October 2026

| Evidence | Result |
| --- | --- |
| Tree | Base `f6f4af5`, plus the L1 correction `22321bc` (`fix/liquid-l1-session-ceiling`) brought in uncommitted, plus lane L2-B (`feat/liquid-l2-eval`) and lane L2-A, all uncommitted on `feat/liquid-l2`. Nothing is committed, merged to `main` or pushed |
| Hermes gate m2 (GCC, 5 October 2026), verbatim | `strict_build_exit=0 warnings=0`; focused `All tests passed (2091 assertions in 45 test cases)`, also with random order; `full_ctest_exit=0 100% tests passed, 0 tests failed out of 37`; `default_exit=0 100% tests passed, 0 tests failed out of 32 authoring_registered=0`; `core_exit=0 100% tests passed, 0 tests failed out of 18 warnings=0`; `authoring_without_sim_configure_exit=1 (expect nonzero)`; `install_exit=0`; `installed_consumers_exit=0 100% tests passed, 0 tests failed out of 4`; `source_consumer_exit=0 100% tests passed, 0 tests failed out of 3`; `asan_exit=0 100% tests passed, 0 tests failed out of 37 san_errors=0`; `tsan_exit=0 100% tests passed, 0 tests failed out of 37 tsan_warnings=0`; `diff_check_exit=0` |
| Focused tags (m2) | `[l2.4]` `All tests passed (407 assertions in 9 test cases)`; `[l2-c1]` `All tests passed (169 assertions in 5 test cases)`; lifecycle `All tests passed (432 assertions in 21 test cases)` |
| Gates (m2) | `G8a core: 0 G8b scripting: 0 G9 private: 0 G10 detail: 0 G11 io: 0 G13: 0 cycles`; `G12: 0 target cycles`; `G8c_outside=0`; `apps_or_scope_includes=0`; `Q8_abs_paths=0`; `Q3_binaries=0`; `ARCHITECTURE_sha=69730447a1d0ef2f (expect 69730447a1d0ef2f)` |
| Test-file integrity (m2) | `lifecycle_hashes: fa18240e f9ea3bc0`; `eval_test_vs_merge: 328c328 1444c1444 1618a1619,2023`; `tests_removed_vs_base: 0`; `r5_diff: 1444c1444 1859a1860,2023` |
| Reviews | L2-A L2.1 review r1: PASS, 0 blocking, 3 non-blocking (L2A-F1–F3). L2-B review r1: NEEDS_CHANGES, 1 blocking (Q17/P11, L2B-13, fixed in fix-r1) and 5 non-blocking (L2B-14–L2B-18); L2-B review r2: PASS, 0 blocking, 3 non-blocking (L2B-20–L2B-22). Combined L2.4/integration review c1: PASS, 0 blocking, 4 non-blocking (L2A-F16–F19). Focused review c2 (round-5 tests and docs): PASS, 5 non-blocking (L2A-F21–F25). The gateway rejected Codex every time; each review was done by a fresh read-only Claude reviewer. Gate m2 ran on the round-5 code; the round-7 cleanup (L2A-F21 test assertion, docs) was verified only by the builder's strict build and tests |
| Not verified | Clang, gcovr coverage, the jscpd clone gate and remote CI (owner rule: GCC only, no tool installs) |
| Round 8 (L2B-14, L2B-22) | Code, tests and docs were changed after gate m2. Every test total and gate line above is from m2; the final gate below supersedes them |
| Final Hermes gate m4 (5 October 2026, after rounds 8 and 9) | GCC on the final code. Strict (Release, strict warnings as errors, Authoring ON): build 0 warnings, `ctest` 37/37. `test_authoring_evaluation`: 53 cases / 2312 assertions, also in random order; `[l2-b14]` 8 cases / 220 assertions; `[l2.4]` 9 cases / 407 assertions; `lua_lifecycle` 21 cases / 432 assertions. Default (Authoring OFF) 32/32 with no authoring test registered; Core-only 18/18, 0 warnings; Authoring without Simulation fails to configure (expected). Installed consumers 4/4, source consumer 3/3. ASan/UBSan 37/37, 0 reports; TSan 37/37, 0 warnings. `git diff --check` clean; design gates G8a–G13, G12 and G8c 0; no Scope include or link; `ARCHITECTURE.md` unchanged; no test line removed versus base `f6f4af5`; no hidden test left. Hermes mutation in a throwaway copy (scoped command filter removed): 2 of 8 `[l2-b14]` cases fail |
| Reviews after m2 | c3 (round 8): PASS, 0 blocking, 7 notes (L2A-F26–F32). c4 (round 9): PASS, 0 blocking, 3 notes: the TRACEABILITY count "7" corrected to "8" `[l2-b14]` cases (fixed by Hermes, docs only); the absent-name assertion is redundant with the exact-text check (accepted); a declared address is not proven to be the real Runtime binding (same as the open follow-up in L2A-F27). Codex was rejected by the gateway; every review was a fresh read-only Claude reviewer |
| Open before approval | None. Re-gate (m4) and reviews of rounds 8 and 9 (c3, c4) are done; L2B-14 was decided by the owner and fixed |
| Owner approval | Approved by the owner on 5 October 2026 (Discord `#liquid-layer`, message 1556746695621083217: "yes i aproove, lets commit and push to github as well"), after a live run of an installed-package demo program (evaluation Passed/Failed cases, host-only world totals, live World unchanged). Commit and push of `feat/liquid-l2` authorized; merging into `main` not requested |


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
