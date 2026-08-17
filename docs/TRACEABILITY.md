# Solid v0.1 Milestone-to-Test Traceability

A row may be marked complete only after its regression first failed against the prior behavior, the fix passed targeted tests, and the strict full suite passed.

| Stage | Contract evidence | Regression evidence | Status |
|---|---|---|---|
| S0 | Approved contracts and branch alignment | Documentation consistency check; strict Release baseline 14/14 | Complete |
| S1 | Namespaces, handles, codecs, mutation safety, system identity | `ids`, `value`, `component_codec`, `intent_registry`, `world`, `runtime`; official Catch2 v3.8.1 | Complete |
| S2 | Canonical values, memory/file stores, recovery, replay | `event_store`, `replay`, v1 golden fixture, decoder fuzz smoke, recovery/retention fault injection, Lua source evidence | Complete |
| S3 | Effects, commands, feedback, retry | `feedback`, `idempotent_dispatcher`, `runtime_effects`; restart, reconciliation, bounded history, durable outbox | Complete |
| S4 | Simulator and canonical scenarios | `simulation_adapter`, `runtime_effects`; full failure matrix, 5,000-target/1,000-frame stress, durable projection/verifier demo | Complete |
| S5 | Package and platform support | Core-only/full source and installed consumers; vendored Lua; CI matrix; sanitizer/fuzzer jobs; coverage gate | Complete — remote CI matrix first passed on `main` 2026-08-17; keeping it green remains an S7 gate |
| S6 | Solid Scope v2 | Bridge, schema, process, dependency-free browser self-test, owner manual browser verification, performance tests | Pending |
| S7 | Release audit | Audit findings and release gates | Pending |

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

These results are implementation evidence, not a release declaration.

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
the same branch. The remaining accepted follow-up is encapsulating the
all-public `RuntimeEffectsState` member bag behind invariant-preserving
operations, deferred as an S7 audit candidate.
