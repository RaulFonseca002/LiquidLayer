# Solid v0.1 Milestone-to-Test Traceability

A row may be marked complete only after its regression first failed against the prior behavior, the fix passed targeted tests, and the strict full suite passed.

| Stage | Contract evidence | Regression evidence | Status |
|---|---|---|---|
| S0 | Approved contracts and branch alignment | Documentation consistency check; strict Release baseline 14/14 | Complete |
| S1 | Namespaces, handles, codecs, mutation safety, system identity | `ids`, `value`, `component_codec`, `intent_registry`, `world`, `runtime`; official Catch2 v3.8.1 | Complete |
| S2 | Canonical values, memory/file stores, recovery, replay | `event_store`, `replay`, v1 golden fixture, decoder fuzz smoke, recovery/retention fault injection, Lua source evidence | Complete |
| S3 | Effects, commands, feedback, retry | `feedback`, `idempotent_dispatcher`, `runtime_effects`; restart, reconciliation, bounded history, durable outbox | Complete |
| S4 | Simulator and canonical scenarios | `simulation_adapter`, `runtime_effects`; full failure matrix, 5,000-target/1,000-frame stress, durable projection/verifier demo | Complete |
| S5 | Package and platform support | Core-only/full source and installed consumers; vendored Lua folded into `Liquid::Lua`; exact public target checks; strict builds, sanitizers, fuzz smoke, and coverage | Complete |
| S6 | Solid Scope v2 | Trace lanes, bounded bridge parsing, complete Unix process-group cleanup, Release assertion guards, 1,000-frame responsiveness, dependency-free browser self-test | Complete — owner accepted the internal-instrument/no-browser-compatibility boundary and waived a repeat manual matrix on 2026-08-22 |
| S7 | Release audit | Regression-first audit fixes, three independent re-reviews, local Linux release matrix, accepted low support boundaries | Complete — 2026-08-22 |

## Liquid L0 — Lua schemas and capability manifests

Steps from the [L0 spec](LIQUID_L0_IMPLEMENTATION_SPEC.md#implementation-steps-and-tests). Test cases are prefixed with their step ID. Each step failed against stubs before its implementation (see the [completion record](DEVELOPMENT_TRACKING.md#l0-completion-record-pending-review-and-approval)).

| Step | Contract evidence | Regression evidence | Status |
|---|---|---|---|
| L0.1 | Immutable schema factories, expanded-tree ceilings, validator and error paths | `lua_schema`: 19 `L0.1` cases | Implemented; awaiting review |
| L0.2 | Four-argument overload, runner-limit fit, write/read schema enforcement, legacy overload | `lua_manifest`: 9 `L0.2` cases | Implemented; awaiting review |
| L0.3 | Manifest projections, failure codes, copied values, ordering, freeze-on-success | `lua_manifest`: 12 `L0.3` cases | Implemented; awaiting review |
| L0.4 | Access expressions, aggregate limits, `{}` versus empty Array, installed surface | `lua_manifest`: 11 `L0.4` cases; `examples/lua_consumer` installed/source consumers | Implemented; awaiting review |

## Local verification snapshot — 13 August 2026

- GCC Release strict warnings-as-errors: 23/23 tests passed.
- GCC ASan/UBSan and TSan full suites passed 23/23, including the
  5,000-target/1,000-frame stress scenario.
- Core-only (`LIQUID_BUILD_LUA=OFF`, Simulation off): 18/18 tests passed.
- Clean source-tree and installed `find_package` consumers passed for Core,
  Lua, and Simulation; old registry/storage/coordinator include paths are absent.
- Core coverage passed at 90.8% line (3,860/4,249) and 80.1% branch
  (3,106/3,879).
- The production audit closed the accepted headless critical, high, and medium
  findings. The remaining gates are remote platform CI and Solid Scope S6.

These results are the historical S0-S5 implementation snapshot.

## Remote CI snapshot — 17 August 2026

- The full 8-job matrix passed on `main`: strict warnings-as-errors Release
  on Linux GCC, Linux Clang, macOS AppleClang, and Windows MSVC; Clang
  ASan/UBSan; Clang TSan; the Core coverage gate (90.5% line, 80.8% branch
  with pinned gcovr 8.3); and the Core-only source and installed consumers.
- The repository now develops on the single `main` branch carrying the
  engine and Solid Scope; the Scope Python bridge regressions are registered
  on Linux only, as documented in `AGENTS.md` and `docs/SUPPORT.md`.

## S7 maintainability finding — closed 17 August 2026

The external codebase analysis flagged `src/Runtime.cpp` (1,427 lines) and
`src/events/FileEventStore.cpp` (1,079 lines) as excessive responsibility
concentrations. Both were decomposed by mechanical, semantics-preserving
extraction: the runtime now spans five units under `src/runtime/` (frame
driver, effects state, evidence, feedback, restore) and the file store three
under `src/events/` (facade, format codec, platform I/O), the largest at 443
lines. Evidence: every moved body verified byte-identical against the
pre-refactor sources, strict suite green after each of the extraction
commits, ASan/UBSan clean, the Core coverage gate unchanged at 90% line, the
golden binary fixture byte-identical, and a high-effort adversarial review
whose confirmed findings (header macro hazard, format-codec linkage, shared
authoritative-commit and status seams, documentation drift) were fixed on
the same branch. The remaining accepted follow-up was encapsulating the
all-public `RuntimeEffectsState` member bag behind invariant-preserving
operations; S7 completed that work without changing the public API.

## Final local release matrix — 22 August 2026

- Strict warnings-as-errors Release suites passed 30/30 with GCC 14 and 30/30
  with Clang 19. The Scope Python bridge and real bridge/runtime integration
  tests are included in those Linux totals.
- Clang 19 ASan/UBSan and TSan suites each passed 30/30.
- The exact CMake Core coverage target passed 18/18 tests and enforced 91%
  line coverage (5,221/5,724) and 80% branch coverage (4,623/5,756). Its gcovr
  scan is constrained to the active binary directory so unrelated build
  artifacts cannot contaminate the result.
- Core-only strict Release passed 18/18; clean source-tree and installed
  Core-only consumers passed. The full install and consumers proved that only
  the documented `Liquid::Core`, `Liquid::Lua`, and `Liquid::Simulation`
  components are exported and no private vendored-Lua target leaks.
- All three Clang libFuzzer smoke tests passed. The dependency-free browser
  self-test, deterministic trace checks, 1,000-frame Scope path, and all 16
  bridge unit tests passed.
- The final audit fixed and pinned complete lifecycle intent rollback,
  canonical checkpoint/retention/restore validation, retained retry progress,
  atomic retry-safe adapter registration, numeric command ordering, bounded
  Scope HTTP framing, and whole-process-group cleanup.
- Independent data-integrity, delivery/scope, and safety re-reviews reported
  no remaining critical, high, or medium blocker.
- The last full remote Linux/macOS/Windows matrix remains the green
  17 August 2026 run. By owner decision, v0.1.0 acceptance uses the final local
  Linux matrix; no additional remote run or manual cross-browser claim is part
  of this release.

## Historical status snapshots

*(formerly `docs/IMPLEMENTATION_STATUS.md`, "Solid v0.1 Implementation Status"; text verbatim, headings demoted one level. Historical, not current authority. The worktree path below is a historical record, not a live location. Its references to `docs/TRACEABILITY.md` now point to this file; `DEVELOPMENT_TRACKING.md` now lives at [docs/DEVELOPMENT_TRACKING.md](DEVELOPMENT_TRACKING.md) and `COMPLETE_SOLID.md` at [docs/history/SOLID_V01_COMPLETION.md](history/SOLID_V01_COMPLETION.md).)*

> Historical implementation snapshot. This file preserves the 13 August 2026
> branch outcome and its later landing/finalization updates.
> `docs/TRACEABILITY.md` and `DEVELOPMENT_TRACKING.md` are authoritative for
> the completed S0-S7 status and current Stage 2 planning boundary.

Date: 13 August 2026

Branch: `feature/solid-v0.1-finalization`

Worktree: `/home/raul/Desktop/tcc-solid-v01`

### Outcome on 13 August 2026

S0-S5 are complete locally, but the release is not complete and
`COMPLETE_SOLID.md` remains intentionally open.
S6 was not started because the headless work has not landed on `main`; the
owner's dirty `experiment/stage2` checkout was not modified.

Implemented work includes:

- `liquid::Value`, component codecs, immutable encoded intent snapshots,
  transactional component replacement, generational behavior/intent/type
  handles, monotonic intent sequencing, owner-thread and overflow guards;
- stable system name/version registration and failed-system evidence;
- canonical event values, memory/file stores, CRC32C records, locking,
  recovery, checkpoints, retention, projection, and record divergence;
- effect commands/reports, bounded feedback, immediate/deferred feedback,
  retries, timeout, supersession, late-report authority, indeterminate
  reconciliation, and a bounded idempotent-dispatch helper;
- an adapter-interface simulator with latency, normalization, negative
  outcomes, duplicates, reversed delivery, silence, and crash injection;
- CMake 0.1.0 static targets, optional Core-only builds, vendored Lua 5.4.8,
  installed component discovery, consumer examples, CI and documentation.

### Verified locally

- GCC Release with strict warnings-as-errors: 23/23 tests passed.
- GCC Debug ASan/UBSan with strict warnings-as-errors: 23/23 tests passed,
  including the 5,000-target/1,000-frame simulator stress case.
- Core-only with Lua and Simulation disabled: 18/18 tests passed.
- Clean source-tree and installed-package consumers passed for
  `Liquid::Core`, `Liquid::Lua`, and `Liquid::Simulation`.
- Decoder fuzz smoke tests passed; Core coverage passed at 90.8% line and
  80.1% branch.
- `git diff --check` passes.

### Release blockers recorded on 13 August 2026

- land the reviewed headless branch on `main` and forward-merge it into the
  visualization track;
- complete the S6 Solid Scope schema, bridge hardening, process supervision,
  incremental rendering, dependency-free self-test, and recorded owner manual
  browser verification;
- execute the configured GCC, Clang, AppleClang, and MSVC CI matrix remotely;
- run the S7 whole-codebase audit and close any new findings.

The authoritative per-stage status is in `docs/TRACEABILITY.md`.

### Update — 17 August 2026

- The headless framework landed on `main`, and the repository was unified on
  a single `main` branch carrying the engine and Solid Scope; the former
  `experiment/stage2` track was fast-forwarded into `main` and retired.
- The remote 8-job CI matrix passed in full on `main`: strict Release on
  Linux GCC, Linux Clang, macOS AppleClang, and Windows MSVC; ASan/UBSan;
  TSan; the Core coverage gate; and the Core-only consumers. The Scope
  Python bridge regressions are Linux-verified (see `docs/SUPPORT.md`).
- Remaining blockers: the S6 owner manual browser verification record and the
  S7 whole-codebase audit.

### Finalization — 22 August 2026

- S6 closed with one Runtime, trace schema v2, bounded bridge validation,
  complete Unix process-group supervision, responsive 1,000-frame rendering,
  and the dependency-free presentation self-test. The owner accepted Scope as
  an internal Linux instrument with no browser compatibility promise and
  waived a repeat manual browser matrix.
- The S7 audit closed lifecycle intent transaction, checkpoint/retention and
  restore validation, adapter-registration atomicity, Scope supervision, and
  package-export findings with regression tests. Independent data-integrity,
  safety, and delivery-scope reviews found no remaining blocker.
- The local Linux release matrix passed under GCC 14 and Clang 19, including
  30/30 full suites, ASan/UBSan, TSan, 18/18 Core-only tests, Core coverage at
  91% line and 80% branch, three libFuzzer smoke tests, Scope bridge tests,
  browser self-tests, install, and source/installed consumers.
- Solid v0.1.0 is complete. Stage 2 is limited to research and approval of its
  first milestone; no Liquid implementation work is current.
