# Liquid documentation validation

**Review dates:** 6–10 September 2026.
**Scope:** documentation/design hardening, not Stage 2 implementation.
**Worktree:** `/home/raul/Desktop/tcc/.worktrees/liquid-doc-hardening` on
`docs/liquid-documentation-hardening`.

> **Dated report.** Sections through "Remaining activation prerequisites"
> record the state of 6–10 September 2026 and are kept unchanged as evidence,
> except the corrected edited-file count and a note marking the hardening
> prerequisite satisfied. For the current state see
> [Post-handoff reconciliation (2026-09-26)](#post-handoff-reconciliation-2026-09-26)
> and [tracking](../DEVELOPMENT_TRACKING.md).

## Outcome and evidence boundary

The documentation now specifies a complete L0–L6 sequence and the Claude
implementation → tests → Codex review → owner gate. Consequential API, authority,
cleanup, limits, evidence and transport choices formerly left to implementation
are recorded in the linked specs. The owner authorized this review and its
creative scope; acceptance of the resulting design and activation remain separate.

This report is one Codex documentation/code review with a subsequent consistency
pass. It is not a separate-agent approval, a Claude implementation result, or
proof that future Liquid code works. No source, build configuration, dependency,
runtime behavior or installed package has been changed by this documentation pass.

## Pinned baselines

| Material | Revision / state | Treatment |
| --- | --- | --- |
| Documentation branch at start | `76f00e414445e75dc698a166f1f01b72fadb88f2` | Eight changed documents, 19 commits ahead of the local main baseline |
| Refreshed origin/main, 10 September | `af5af08befea` | Already an ancestor of this branch; no missing main commits |
| Committed Solid hardening | `5979e149e042` | Eleven code/doc commits beyond the common baseline; reviewed as intended foundation |
| Current root worktree | `fix/pre-liquid-hardening` plus unfinished edits | Not merged, stashed, reset, or included in baseline test results |

Unfinished root edits included Runtime.hpp, runtime-effects tests, tracking,
changelog, an untracked verification document and `.claude/` material. These
remain owned by that task. The documentation branch itself still contains the
older source snapshot; this pass did not merge the hardening branch into it.

The baseline run used `/tmp/tcc-solid-doc-validation` and
`/tmp/tcc-solid-doc-validation-build` on 6 September. Those temporary directories
were cleared between sessions; the recorded command results remain the evidence.
The documentation edits were recovered from the saved tool-call history on
7 September into the persistent worktree named above. The original root edits
were preserved. Before implementation, land required
hardening, settle unfinished fixes, reconcile the docs with the exact landed
revision, and rerun the baseline there. Do not infer that today's clean baseline
test makes a different pending source revision ready.

## Findings and design closure

"Specified" below closes a documentation gap. The cited future tests remain
mandatory implementation evidence, not passed tests.

| ID / severity | Evidence and consequence | Correction and required evidence |
| --- | --- | --- |
| D01 High | Original tracking/roadmap labeled L1–L6 provisional and deferred scope/revision choices; Claude could not execute a complete sequence | All seven specs include interfaces, steps, oracles, allowlists and gates; common contract supplies shared rules |
| D02 High | `LuaLifecycleSystem::run` reads one `scriptName` for all members; components are globally named per type | L2.1 adds opt-in SingleReadable selection with independent-source/legacy tests; no source change made here |
| D03 High | Preparing a discovery behavior in live World can invoke membership callbacks even without Lua source | L1 uses a host-owned scope and runner callbacks; tests assert zero topology/intent/system changes |
| D04 High | Old L1 deferred representation and stale identity; same-name target replacement could reuse approval accidentally | Exact private generational targets, scope/policy revisions and runner identity are checked at each boundary; L1.4/L5.1 test ABA and revocation |
| D05 High | Old L5 left fate of persistent intents after source revision open; lifecycle state reset does not delete intents | L5 replaces the Solid behavior instance and removes all old intents; test both winning and losing old desires |
| D06 High | World lifecycle callbacks may throw after committed structural changes; multi-action activation is not transactional | L5 defines pre/post-retirement failure, actual-liveness checks and NeedsIntervention; inject failure at each stage |
| D07 High | Last FrameLog selections use type/name and may predate cancellation or same-name recreation | L3 adds target provenance and separates current capture from historical selection; no query-driven resolution |
| D08 High | `Intent::encodedValue` uses ComponentCodec; Lua read/write schemas may describe different representations | L0 keeps distinct schemas; L3 converts desired component values through trusted codecs before describing them as Lua reads |
| D09 High | Existing `fnv1a64:` source evidence is diagnostic, not an authenticated artifact identity | Host-owned immutable records plus exact source/scope/evaluation binding; L5 rejects substitution regardless of a matching displayed hash |
| D10 High | Evidence was after live operations and MCP before approval; operation evidence/authority would be incomplete | Reorder L4=journal, L5=operations, L6=transport; update every active milestone index |
| D11 High | Old L0 named limit categories but left effective limits, API form and freeze behavior open | L0 fixes overload, factories, exact bounds, atomic registration and successful-capture freeze; boundary and legacy tests |
| D12 High | JSON conversion can lose int64 precision, float identity, arbitrary bytes and empty Array | L6 defines tagged wire values, bounded parsing and byte identity tests; no JSON Schema coercion becomes native authority |
| D13 High | Separate scope data/visible competitors could disclose unreadable state or other callers' artifacts | CallerContext checks, Write-only suppression, explicit competitor allowlist, private fixture inputs, cross-scope tests |
| D14 Medium | Old workflow assigned substantive implementation to owner/Codex and required Hermes-oriented setup | Claude implements activated work; Codex reviews; owner advances; Hermes optional; no model/CLI setup prerequisite |
| D15 Medium | Agent, roadmap and workflow documents had differing authority orders and repeated approval status | One rule in AGENTS; specs describe proposals, focused contracts describe existing behavior; no step auto-activated |
| D16 High | Event/file/replay guides promised rollback on every retention failure | Correct pre-install versus post-install directory-flush boundary using FileEventStore platform/retention behavior |
| D17 High | Physical retention could be mistaken for indefinitely bounded history; checkpoint embeds historical arrays | Document finite-history Value limits, backed by committed replay regression; L4 makes no durable/indefinite journal claim |
| D18 Medium | Solid completion and historical notes could be read as current defect-free guarantees | Date completion findings; route current status through tracking; keep historical snapshots explicitly historical |
| D19 Medium | New evaluation functionality cannot be placed in Lua while linking Simulation back into it | Opt-in Authoring target depends on Core/Lua/Simulation; old exports unchanged when disabled; L1 package tests |
| D20 Medium | Scenario evaluation could be mistaken for live-state cloning, generic source proof or human suitability | Host-registered finite fixtures, frozen inputs, real isolated Runtime, normalized traces and explicit Passed/NotRun semantics |
| D21 Medium | Native operation recording could fail after a topology action; full journal could block stop | Pre-reserved start/terminal records and stop capacity; capacity/failure tests; no claim of crash atomicity |
| D22 Medium | Unpinned future MCP/SDK direction left language, process/threading and cancellation unresolved | L6 fixes local Python SDK + C++ child architecture, releases, framing, tool allowlist and teardown tests |
| D23 High | A scope type defined only in Authoring would make the Lua runner depend back on Authoring | L1 defines LuaScopeGrant in Lua; Authoring aliases it; no reverse link/header dependency |
| D24 High | Inspection originally assumed managed records that are introduced only in L5 | L3 has a host-selected native behavior allowlist and capture-local references; L5 managed references are optional additions |
| D25 Medium | State inspection alone omits retained command/report facts and can hide pruned evidence | L3 includes bounded latest/authoritative command summaries, observation revision and explicit HistoryUnavailable |
| D26 Medium | Tagged JSON adds structural nesting beyond native Lua/Value depth | L6 separates 128-level wire parsing from native limits and requires maximum-depth wrapped round-trip tests |

## Code-to-contract trace

| Existing source | Fact used by the design |
| --- | --- |
| `include/liquid/scripting/LuaBehaviorRunner.hpp` | Exact LuaValue kinds; independent encode/decode; execution limits and private typed bindings |
| `src/scripting/LuaBehaviorRunner.cpp` | Registration freeze/name validation, safe string-key access, array marker and transactional execution |
| `include/liquid/world/World.hpp`, `src/world/World.cpp` | Public live-intent queries, world/access identities, typed host access and private Runtime helpers |
| `include/liquid/detail/ComponentRegistry.hpp` | Existing name/slot/encoding queries; missing non-template schema lookup explicitly added to L3 allowlist |
| `src/scripting/LuaLifecycleSystem.cpp` | Fixed script-name selection and revision/source watch-state reset |
| `include/liquid/Intent.hpp` | Owner/target/lifetime/priority/sequence and ComponentCodec-encoded intent value |
| `include/liquid/Runtime.hpp`, `src/runtime/` | Sole frame driver, last-frame evidence, private external bindings, authoritative observations and fault behavior |
| `apps/SimulationScenario.cpp` | Existing light-specific setup and real Runtime/adapter loop, not a generic installed evaluator |
| `include/liquid/simulation/InMemoryAdapter.hpp` | Explicit-time delivery, bounded simulated reports, observations and adapter outcomes |
| `include/liquid/events/MemoryEventStore.hpp` | Existing record-count bound; L2 supplies a private additional byte-budget wrapper |
| `src/events/FileEventStore.cpp`, `src/events/FileEventStoreIo.hpp` | Retention replacement/directory durability boundary |
| `tests/test_replay.cpp` at `5979e149e042` | Checkpoint-history node-limit regression; not claimed present in the older documentation source tree |
| `CMakeLists.txt`, `cmake/`, `.github/workflows/ci.yml`, `examples/` | Current package exports, optional dependency direction, exact CI/test/consumer gates |

The source directory references above identify bounded review areas, not
permission to implement across those directories. Specs own future file lists.

## Documentation coverage and disposition

The starting tree contained 34 non-vendor Markdown documents. All were inventoried
and assessed for authority/current relevance. Active Liquid docs received full
design review; focused contracts/guides were checked against their relevant code.
Historical research was checked for routing and stale authority, not re-certified
as current scientific evidence. Licenses were not reinterpreted or changed.

| Group | Starting documents | Disposition |
| --- | --- | --- |
| Active Liquid | `AGENTS.md`, `CLAUDE.md`, `DEVELOPMENT_TRACKING.md`, `README.md`, `Liquid_Concepts_and_Architecture.md`, `docs/LIQUID_STAGE2_PLAN.md`, `docs/LIQUID_L0_IMPLEMENTATION_SPEC.md`, `docs/HERMES_MULTI_MODEL_DEVELOPMENT_GUIDE.md` | Rewritten/synchronized; remove stale role and milestone assumptions |
| Focused Solid contracts | `docs/ADAPTER_CONTRACT.md`, `docs/COMPATIBILITY.md`, `docs/PUBLIC_API.md`, `docs/LIFECYCLE_SCRIPTING.md`, `docs/SECURITY_BOUNDARY.md`, `docs/THREADING.md`, `docs/EVENT_FORMAT_V1.md`, `docs/REPLAY.md` | Preserve implemented contracts; correct retention documentation; future additions stay in specs |
| Operational guides | `docs/ADAPTER_GUIDE.md`, `docs/INTEGRATION_GUIDE.md`, `docs/REPLAY_GUIDE.md`, `docs/FILE_FORMAT_GUIDE.md` | Check actual API/commands; synchronize retention failure/history limits |
| Support/evidence | `docs/SUPPORT.md`, `docs/TRACEABILITY.md` | Preserve dated matrices and supported instrument boundaries |
| Historical material | `COMPLETE_SOLID.md`, `CURRENT_STATE_EVALUATION.md`, `docs/IMPLEMENTATION_STATUS.md`, `M6_TEST_BASE.md`, `ARTICLE_NOTES.md` | Date audit conclusions; preserve existing historical banners and research scope |
| Scope product/design | `PRODUCT.md`, `DESIGN.md`, `apps/visualizer/README.md` | Explicitly identify Scope as the instrument; no engine/application redesign implied |
| Package guide | `cmake/README.md` | Existing exports remain current; link future opt-in Authoring proposal |
| Release history | `CHANGELOG.md` | Preserve release history; documentation work is not a new engine release |
| Legal/vendor notices | `LICENSE-NOTICE.md`, `THIRD_PARTY_NOTICES.md` | Preserve; future L6 implementation must add its actual dependency notices |

New documents are the shared implementation contract, this report, and L1–L6
specifications, all under existing `docs/`. No source directories were created.

## Verification performed

Committed code baseline `5979e149e042`, GCC 14.2.0, Release:

```sh
cmake -S /tmp/tcc-solid-doc-validation -B /tmp/tcc-solid-doc-validation-build -DCMAKE_BUILD_TYPE=Release -DLIQUID_ENABLE_STRICT_WARNINGS=ON -DLIQUID_WARNINGS_AS_ERRORS=ON
cmake --build /tmp/tcc-solid-doc-validation-build --parallel 4
ctest --test-dir /tmp/tcc-solid-doc-validation-build --output-on-failure
```

Configure/build succeeded; **30/30 tests passed**, including lifecycle,
runtime/effects, replay, simulation, bridge/integration and presentation tests.
CTest reported 11.22 seconds. This rerun does not claim fresh Clang, sanitizer,
coverage, portability or remote CI results; their future milestone gates remain
required in the common contract.

Final consistency pass on 7 September:

- 42 non-vendor Markdown files checked; the inventory accounts for all 34
  starting documents, plus the eight new specifications/contract/report files.
- 115 local Markdown/image links and 20 heading anchors resolved.
- All code fences were balanced; 14 shell blocks passed `bash -n` syntax checks.
  Parsing a future command is not a claim that its future executable exists.
- All seven specs contain four ordered steps (28 total), an allowed-file list,
  focused test commands and an explicit inactive status. Existing CTest names
  were checked against the committed baseline; new names are future gates.
- Cross-milestone review closed the Lua/Authoring type cycle, pre-L5 inspection
  mapping, scope clock, typed grant helper and retained-effect evidence gaps.
- `git diff --check` passed. The task worktree contains 15 edited existing
  Markdown files and eight new Markdown files, with no non-Markdown changes.
  (Corrected on 26 September from an original count of 14; the file set
  itself is unchanged.)
- The original root checkout retains its original tracked/untracked edits.
  No commits, pushes, merges or implementation activation were performed.

No blocking documentation finding remains in this review. This is readiness
for owner design review and later implementation gates, not implementation approval.
Future L0–L6 test names and APIs are specification requirements, not existing
executables or successful test runs.

## Remaining activation prerequisites

1. Owner accepts the revised design, including independent script selection,
   bounded inspection, opt-in Authoring packaging, replacement cleanup and
   the finite in-memory/local transport boundary.
2. Required Solid hardening and any separately resolved outstanding fixes
   land; recheck the documentation against that exact source revision.
   *Satisfied on `main` by 26 September; see the reconciliation below.*
3. Record owner activation of L0.1 with its allowed files and specification
   revision. Claude implements it, then Codex reviews actual implementation
   evidence before the owner permits L0.2.

These are explicit approval/integration gates, not unspecified implementation
design choices. No engine milestone is marked implemented, tested or approved
by this report.

## Post-handoff reconciliation (2026-09-26)

This section reconciles the dated report above with `main` as of `f3ef421`.
It is a documentation reconciliation only: **no new build or test run was
performed for it**. A fresh build on the landing revision is a separate step.

- **Main moved from `8003ee6` to `f3ef421`.** In that range: the pre-Liquid
  Solid hardening merged (`3e202ef`, containing `5979e14` and the retention/
  audit/Scope doc correction `ba8269c`); `2aee8a4` followed it so a recreated
  component immediately reclaims its effect target; `feat/atech` merged
  (`cdd85b4`) outside the Liquid docs; and `f3ef421` added blind Core coverage
  tests. The pre-Liquid hardening is therefore landed, not pending.
- **Weave, not overwrite.** The hardening documents were layered onto `main`.
  Main's newer text was kept for `DEVELOPMENT_TRACKING.md` (full pre-Liquid
  hardening record), `COMPLETE_SOLID.md`, `PRODUCT.md` and
  `docs/EVENT_FORMAT_V1.md`. Status text was updated so that no document
  claims hardening is pending or that documentation integration is current.
- **Preserved material.** The old Stage 2 plan's 28 August external-research
  notes are a dated appendix in [the roadmap](LIQUID_STAGE2_PLAN.md); the old
  workflow's preflight, conflict report and evaluation metrics are compact
  sections of [the workflow](HERMES_MULTI_MODEL_DEVELOPMENT_GUIDE.md).
- **L3 baseline.** `2aee8a4` fixes stale effect-binding authority but not
  `FrameLog`'s name-keyed selections; L3 target provenance remains proposed.
- **Owner decisions recorded 26 September.** L0 is not active and no Stage 2
  step is active until separately activated; the Claude-implements /
  Codex-reviews / owner-approves roles replace the owner-only `.cpp`
  convention; the L0–L6 order, specs and L6 dependency pins (Python MCP
  2.1.1, nlohmann/json 3.12.0) are accepted as specified-but-inactive design.
- **Review finding superseded.** The "no blocking documentation finding"
  statement above applied to the 10 September worktree. The later integration
  review found weave-blocking issues (overwriting newer main text, stale
  status, lost research/workflow material, and missing L4–L6 files); this
  reconciliation addresses them.
