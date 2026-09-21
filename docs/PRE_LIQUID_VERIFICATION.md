# Pre-Liquid hardening verification

Date: 5 September 2026.

Reviewed commit: `5979e149e042e9bc0c4089a3fbd24693e11b5399`, branch `fix/pre-liquid-hardening`.

Updated after the authorized binding fix in the working tree based on that commit.

## Verdict

**The remaining binding-lifecycle defect is fixed, and both full local test suites pass.** No code finding from this verification remains open. L0 kickoff still awaits integration of the already-approved Stage 2 documentation and the normal CI/merge gates below.

The follow-up preserves rejection of duplicate live bindings and verifies that authoritative observations reach the replacement component.

## Binding-lifecycle finding and resolution

Original severity: medium. Related finding: B2. Status: fixed in the working tree.

Before the follow-up, [`Runtime::bind_effect_component`](../include/liquid/Runtime.hpp#L208) checked `componentsByEffectTarget` before retiring entries for removed components. Dead-binding retirement occurred during [`run_frame()`](../src/runtime/Runtime.cpp#L214).

Reproduced against the reviewed strict Core library before the fix:

1. Bind a component to an external target.
2. Remove the component and create its replacement.
3. Bind the replacement to the same target without running an intervening frame.

```text
immediate rebind failed: effect route and target are already bound
rebind after frame succeeded
```

The old reverse entry refers to a dead component generation. It should not reserve that physical target against an explicitly bound replacement.

**Applied correction:** use the existing `current_effect_binding()` lookup before the uniqueness check. It retires a dead entry for the requested target and returns a live binding for conflict validation. All validation and binding changes remain on the owner thread.

**Added regression:** [`test_runtime_effects.cpp:1749`](../tests/test_runtime_effects.cpp#L1749) removes, recreates, and explicitly rebinds to the same target without an intervening frame. It also verifies rejection of duplicate live bindings and correct observation projection. The test failed on the pre-fix code with the original exception (exit 42), then passed all six assertions with the fix (exit 0). The previous removal test ran frames before rebinding and therefore missed this sequence.

## Documentation integration required before implementation

The hardening branch starts from `origin/main` at `af5af08`. The original review used the documentation branch at `76f00e4`. Their engine baseline was the same, but the planning documents have not been integrated into the hardening branch.

Consequently:

- `docs/LIQUID_L0_IMPLEMENTATION_SPEC.md` and `docs/LIQUID_STAGE2_PLAN.md` are absent here.
- [`AGENTS.md:21`](../AGENTS.md#L21) still limits Stage 2 to research and milestone approval.
- [`DEVELOPMENT_TRACKING.md:643`](../DEVELOPMENT_TRACKING.md#L643) still says no Liquid implementation scope is approved.

Bring the already-approved L0 specification, Stage 2 plan, and operational context from `docs/liquid-stage2-plan` into the combined development state. Preserve the new hardening evidence when reconciling `DEVELOPMENT_TRACKING.md`; it is the only path changed by both branches relative to their common base. This is reconciliation of existing decisions, not a request to approve the architecture again.

Keep the owner's selected external-agent integration priority. Keep the checkpoint-history limitation deferred to the separately specified work needed before long-running operation; it is not an additional L0 blocker.

## Verification performed

| Check | Fresh result |
| --- | --- |
| GCC Debug build with strict warnings and warnings as errors | Passed |
| Full strict CTest suite after the fix | 30/30 passed, 9.33 seconds |
| GCC ASan/UBSan Debug build | Passed |
| Full ASan/UBSan CTest suite after the fix | 30/30 passed, 19.11 seconds |
| Immediate replacement-binding regression | Failed before the fix; passed after it |
| Independent reviews of the prior hardening plus follow-up diff review | No further blocking implementation findings |

Commands:

```sh
cmake --build build -j 2
ctest --test-dir build --output-on-failure -j 4
cmake --build build_sanitized -j 2
ctest --test-dir build_sanitized --output-on-failure -j 4
```

The existing build caches were checked: strict warnings and warnings-as-errors were enabled in both; ASan/UBSan was enabled in `build_sanitized`.

## Findings verified as addressed

- **B1:** lifecycle callback lookup, argument construction, and execution are inside a protected Lua call. VM ownership and buffered transaction failure paths are preserved. The new tests exercise the original memory budgets, queued proposals/watches, and recovery.
- **B2:** superseded targets no longer project into rebound components; removed bindings are retired before feedback projection. The follow-up also permits immediate explicit rebinding to a replacement component.
- **B3:** waiters consume the shared completed outcome independently of LRU eviction, with command-identity checks and synchronized publication. Its stress regression improves coverage, although the completion/eviction wake-up ordering remains scheduler-dependent; source review is part of the evidence.
- **B4/B5:** completed removals retain evidence despite callback exceptions, and effects-path failures publish the failed frame log. New tests cover replay and failures while recording failure evidence.
- **B6:** blank brightness is rejected and explicit zero remains valid; the parser cases run in the full suite.
- **C1/C3/C4:** event records grow geometrically, Lua evidence emission is shared, and the capability cache is bounded with revision-aware recomputation.
- **C2/D1/D2:** the finite-session checkpoint limitation is documented and tested; retention's replacement boundary and historical audit/product scope are clarified. The separate Stage 2 branch discrepancy still needs reconciliation as described above.

Optional follow-up coverage, not additional blockers: cache overflow with 257 simultaneously live owners, and Lua lifecycle memory exhaustion after queuing cancellation of an existing intent.

## Merge and advancement gates

Remote CI status could not be verified: GitHub CLI returned `Could not resolve to a Repository with the name 'RaulFonseca002/tcc'`. This does not establish either a passing or a failing remote run. No fresh local Clang, TSan, coverage, Release, cross-platform, fuzz, or consumer-package matrix was run.

[`AGENTS.md:69`](../AGENTS.md#L69) requires the strict suite and green remote CI through the normal merge path. The checked workflow runs on `main` pushes, pull requests, or manual dispatch; merely pushing this short-lived branch does not trigger that matrix. Correct the closure note's broader “on push” wording when updating its evidence.

To proceed:

1. **Completed:** fix the immediate-rebind case and add its regression.
2. Integrate the approved planning documents while preserving hardening closure evidence.
3. Reverify the combined revision and confirm the standing CI/merge gates.
4. Start the bounded L0 implementation from the integrated `main`, with headers and tests first.

No other defect identified in this verification requires widening L0 or implementing the later roadmap now.
