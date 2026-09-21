# Pre-Liquid code and product review

**Date:** 5 September 2026  
**Reviewed revision:** `76f00e414445e75dc698a166f1f01b72fadb88f2` (`docs/liquid-stage2-plan`)  
**Purpose:** assess Solid and the documented Liquid direction before L0 implementation.  
**Status:** analysis and recommendations; the findings below have not been fixed by this review.

## Assessment

Keep the architecture and the narrow L0 milestone. The important ownership decisions are already coherent: `World` owns state and access; `Runtime` owns frame execution; Lua submits typed, transactional intent bundles; adapters report external truth; Liquid adds a semantic authoring surface; Liquid Layer chooses models and application policy. The framework does not need another runtime, a generic agent abstraction, or a provider layer to start Liquid.

The immediate work should be a focused Solid hardening pass. The existing strict suite passes, but targeted probes reproduced failures in Lua error containment, effect-binding lifecycle, concurrent dispatch, removal evidence, and failure inspection. Those failures matter more than cosmetic cleanup because future agents will depend on the same authority and evidence boundaries.

There is also a demonstrated event-store allocation problem and a checkpoint capacity limitation. Neither should be hidden behind claims that the runtime is ready for indefinitely long sessions. Product documentation should distinguish a verified deterministic engine from the still-unproven benefit of a neurodivergent-support application.

**Owner decision confirmed during this review:** preserve external-agent integration as the next product proof after the necessary semantic milestones. Keep MCP before approval/activation in the provisional roadmap. Do not reorder that roadmap around a local activation demo.

## Scope and verification

The review covered the public C++ boundary, World/Runtime ownership and lifecycle, registries, Lua execution, effects and feedback, event storage/replay, build and CI configuration, representative Solid Scope input handling, and the current and historical product documents. Vendored dependencies were not subjected to a separate security audit. Source locations below refer to the reviewed revision.

| Check | Result |
| --- | --- |
| Strict existing Debug build, GCC, `LIQUID_ENABLE_STRICT_WARNINGS=ON`, `LIQUID_WARNINGS_AS_ERRORS=ON` | Passed |
| `ctest --test-dir build --output-on-failure -j 4` | 30/30 passed; the simulation-adapter test took about 148 seconds |
| Existing ASan/UBSan build | Passed |
| ASan/UBSan tests for Lua behavior/lifecycle, Runtime/effects, World, event store, replay, and idempotent dispatcher | 8/8 passed |
| Focused failure probes against the built libraries and the actual Scope parser | Findings recorded below |

The first full-suite invocation was interrupted by a tool timeout. The subsequent run completed successfully; its `LastTest.log` records all 30 passes. Old entries in `LastTestsFailed.log` were not counted as current failures. The sanitizer command was:

```sh
ctest --test-dir build_sanitized --output-on-failure -R '^(lua_behavior|lua_lifecycle|runtime|runtime_effects|world|event_store|replay|idempotent_dispatcher)$' -j 4
```

No new full TSan, coverage, Release, cross-platform, fuzz, or consumer-package matrix was run in this review. The probes were temporary checks, not committed regression tests. Passing existing tests does not cover the new triggering cases.

## Confirmed defects

### B1 — High: Lua lifecycle argument allocation can abort the host

**Evidence:** [`LuaBehaviorRunner.cpp:1166`](../src/scripting/LuaBehaviorRunner.cpp#L1166), with the lifecycle call entered at line 1323. `push_frame()` and `push_changes()` allocate Lua objects before the callback's `lua_pcall()`, after the protected top-level script call has already returned. An allocation failure there has no enclosing Lua protected call.

**Reproduction:** call `execute_lifecycle()` with an empty `on_components_changed` callback and 16 changes, each containing two 4 KiB strings. With `maxMemoryBytes` set to 16,384, 24,576, 32,768, 65,536, or 131,072, the process terminated with `SIGABRT`. At 8,192 bytes, the earlier protected initialization path instead returned `MemoryLimitExceeded`. The failure is in host argument preparation, not in a deliberately crashing native codec.

**Impact:** a configured Lua memory bound can terminate the application instead of yielding a bounded execution result. This directly undermines the boundary future generated behavior will use.

**Correction:** put lifecycle callback lookup, argument construction, and invocation inside a protected Lua entry point. Ensure C++ exception cleanup closes the VM, and do not rely on a C++ catch or a VM guard alone to contain a Lua panic. Keep nontrivial C++ object lifetimes safe across Lua error exits.

**Regression:** exercise memory exhaustion while building both frame and change arguments, including after `on_start` has queued an intent. Require a bounded failure result, no committed intents/cancellations/watches, and normal execution of a subsequent behavior. Use a subprocess test where necessary so a regression reports a failed test instead of terminating the whole test executable.

### B2 — High: effect bindings retain authority for obsolete targets

**Evidence:** [`Runtime.hpp:176`](../include/liquid/Runtime.hpp#L176) removes the forward binding when control changes but leaves `componentsByEffectTarget`. Rebinding at line 206 also leaves the former reverse key. [`RuntimeEvidence.cpp:244`](../src/runtime/RuntimeEvidence.cpp#L244) follows that reverse key without verifying that the current binding still names the incoming target. Component removal can leave both indexes holding a stale component handle.

**Reproduced cases:**

- Bind one component to `old`, then to `new`; an authoritative observation for `old` with value 90 changes the newly bound component to 90.
- Change the formerly external component to `InternalState`; an old-target observation faults Runtime with `effect target binding is inconsistent`.
- Remove a bound component; a later observation faults Runtime with `stale component slot handle`.

**Correction:** make forward/reverse binding updates one operation, remove superseded keys, and retire dead component bindings before feedback projection. Check the current route, target, and component generation before decoding into World. Old-target feedback can remain valid evidence about that physical target without gaining authority over a newly bound component.

**Regression:** cover rebind, conversion to internal control, component removal/recreation, stale queued reports as well as unsolicited observations, and reuse of a formerly bound target. Current-binding feedback must continue to work.

### B3 — High: concurrent duplicate dispatch can execute twice after cache eviction

**Evidence:** [`IdempotentDispatcher.cpp:55`](../src/effects/IdempotentDispatcher.cpp#L55) stores only completion/failure in the shared in-flight entry. Waiting duplicates wake at line 188 and loop back to the bounded outcome cache. Another dispatch can evict the completed outcome before a waiter reacquires the lock.

**Reproduction:** with one memory-cache entry and two concurrent-dispatch slots, hold command A while duplicate A calls wait. Finish A and dispatch B so B evicts A. A concurrent probe with 16 duplicate waiters observed two adapter executions of A. This is an overlapping-waiter defect, not a claim that a bounded cache must remember every historical command forever.

**Correction:** retain the original command identity and successful outcome in the shared in-flight completion object. Existing waiters should validate and consume that outcome directly, independently of LRU eviction. Preserve existing failure propagation and same-key/different-command rejection.

**Regression:** force the completion/eviction/waiter schedule with synchronization rather than timing sleeps; require exactly one adapter call for the overlapping A requests. Also cover command mismatch, failure, and the durable-cache path. The existing duplicate tests do not exercise this eviction schedule.

### B4 — High: completed removals can disappear from evidence when callbacks throw

**Evidence:** [`Coordinator.cpp:57`](../src/world/Coordinator.cpp#L57) completes behavior destruction before rethrowing a removal callback's exception. [`World.cpp:59`](../src/world/World.cpp#L59) records the tombstone only after that call returns. Component removal has the same ordering at [`World.hpp:546`](../include/liquid/world/World.hpp#L546).

**Reproduction:** register a system whose `on_behavior_removed` throws, clear the initial evidence buffers, and remove its behavior or its required component. A probe using the supported component-codec registration API produced:

```text
destroy: exists=0 topology_records=0
remove: exists=0 topology_records=0 component_records=0
```

**Impact:** live state and projected evidence can disagree. The existing World tests correctly require cleanup despite callback exceptions, but do not require the corresponding removal evidence.

**Correction:** preserve the established completed-cleanup semantics and record the removal that actually happened before propagating the callback error. Distinguish this case from an operation that failed before removal. Do not solve it by swallowing errors or by changing deletion into an undocumented rollback.

**Regression:** extend the throwing-removal cases to check topology/component tombstones and replayed state, including when a Runtime frame subsequently fails. Verify that unsuccessful pre-removal operations produce no false tombstone.

### B5 — Medium: effects failures leave `last_frame_log()` reporting the previous success

**Evidence:** [`Runtime.cpp:231`](../src/runtime/Runtime.cpp#L231) builds a failed `result.frame` and rethrows, bypassing the assignment to `latestFrameLog` at line 274. Failures inside `execute_frame_phases()` have another update path; effects/feedback/persistence failures do not consistently use it.

**Reproduction:** complete a frame at time 50, then cause an effects failure at time 100. Runtime reports `faulted() == true`, while `last_frame_log()` still has `completed == true` and `now == 50`.

**Correction:** publish the constructed failure log before rethrowing. Preserve the original failure even if appending failure evidence also fails. Share the small failure-finalization operation where that reduces divergent behavior.

**Regression:** after a successful frame, inject failures before system execution and after resolution/dispatch. Require the failed frame's identity, time, completion flag, phase, and message through the public inspection API. Include failure while recording failure evidence. This should be fixed before a Liquid runtime view depends on that API.

### B6 — Medium: Solid Scope silently interprets blank brightness as zero

**Evidence:** [`index.html:41`](../apps/visualizer/index.html#L41) disables native form validation while brightness remains required. [`app.js:700`](../apps/visualizer/app.js#L700) converts the raw field with `Number()`, and the later range check accepts `Number("") === 0`.

**Reproduction:** running the actual `parseScenario()` function with the brightness field cleared returned `initial_brightness: 0` without an error.

**Correction:** reject empty or whitespace-only required numeric input before conversion; keep explicit `0` valid. Use the existing field error/focus convention. Apply the same check to sibling required numeric fields only where their current parsing has this defect.

**Regression:** blank, whitespace, explicit zero, valid endpoints, and out-of-range input. A focused parser test is sufficient; this does not need a new browser harness.

## Capacity and code improvements

### C1 — Demonstrated performance issue: event appends repeatedly relocate history

[`MemoryEventStore.cpp:55`](../src/events/MemoryEventStore.cpp#L55) reserves `records.size() + events.size()` before each append. [`FileEventStore.cpp:278`](../src/events/FileEventStore.cpp#L278) has the same pattern. For single-event appends on the tested standard library, this defeats geometric vector growth and repeatedly relocates all earlier records.

A probe linked against the existing Debug Core library appended minimal `FrameStarted` events to `MemoryEventStore`:

| Records | Elapsed seconds |
| --- | ---: |
| 2,000 | 0.673 |
| 4,000 | 2.710 |
| 8,000 | 10.950 |

Doubling input took roughly four times as long, consistent with the source-level quadratic relocation pattern. These are local diagnostic measurements, not Release throughput claims; they do not establish that this is the only cost in the simulation stress test.

**Change:** grow retained-record capacity geometrically when needed, with overflow checks and the existing record cap. Preserve allocation/preparation before durable side effects. Keep exact-size reservation for genuinely one-shot local arrays; avoid applying this rule mechanically to every `reserve()` call.

**Validation:** preserve event order, sequence exhaustion, batch rejection, and file fault-injection tests. Repeat the append probe in the same build profile and the large simulation test. Avoid a fragile wall-clock threshold in CI.

### C2 — Confirmed capacity limitation: checkpoint retention embeds historical growth

[`Replay.cpp:393`](../src/events/Replay.cpp#L393) serializes historical frames, attempts, reports, observations, scripts, and other record arrays into every checkpoint. Retention removes physical records before the checkpoint while those records remain embedded in its payload. [`ValueLimits`](../include/liquid/Value.hpp#L13) permits 4,096 array items and 16,384 total nodes.

A synthetic accepted stream with 3,000 minimal frame-start records could be checkpointed and retained to two physical records, but still had 3,000 embedded historical frame records. After another 1,000 records, constructing the next checkpoint failed with `value node limit exceeded`. The threshold depends on payload size and event mix; this is not a measured 4,000-frame runtime limit.

This is a demonstrated finite-session limitation, not proof of corrupted stored evidence. It is particularly relevant to long-running agent or smart-environment use, where physical retention alone may appear to promise bounded history.

**Near-term action:** document that limitation and add a repeated checkpoint/retention regression that makes the current behavior explicit. Do not market retention as indefinitely bounding session history, increase all global Value limits, or silently discard history needed by current replay equivalence.

**Before long-running operation:** make a separate checkpoint design decision about bounded current state versus historical evidence, with compatibility tests and an explicit payload/version strategy if the contract changes. Keep this out of L0's schema implementation and preserve frozen Event Format v1 until that change is separately specified.

### C3 — Useful simplification: one Lua execution-evidence helper

[`LuaBehaviorRunner.cpp:1433`](../src/scripting/LuaBehaviorRunner.cpp#L1433) and line 1478 duplicate the source hash, hexadecimal rendering, source-retention decision, and execution-evidence construction. Extract one source-private operation and preserve the exact existing evidence bytes. Test parity between `execute()` and `execute_lifecycle()` in full-source and hash-only modes.

The 1,526-line runner contains distinct concerns, but its length alone does not justify a rewrite. Extract the protected lifecycle entry and this evidence operation first. During L0, keep schema validation and manifest assembly in the already planned scripting files; avoid introducing a generic scripting framework to make this file smaller.

### C4 — Static lifetime concern: the capability cache outlives destroyed behaviors

[`LuaBehaviorRunner.cpp:583`](../src/scripting/LuaBehaviorRunner.cpp#L583) caches descriptions by world and generational behavior ID. The cache is cleared on a world switch, but no same-world removal/eviction path is present. Repeated creation, execution, and destruction in one World can accumulate dead-generation entries even when few behaviors remain live.

Bound or prune this optional acceleration data while preserving current access-revision checks. Eviction must only cause recomputation, never change permissions. Add a behavior-churn check before using the runner for long-lived authoring services. This finding is based on source/lifetime analysis; no heap-growth benchmark was performed.

### C5 — Keep new tests on the supported consumer boundary

The existing test executables enable `LIQUID_ENABLE_LEGACY_INTERNAL_COMPONENT_REGISTRATION` privately in CMake. That is useful for internal tests but can hide mistakes in examples intended for actual consumers. New L0 fixtures should use ordinary public World registration with a real `ComponentCodec`, then the Lua codec and model metadata. Compile a small `Liquid::Lua` consumer exercising both schema-less and model-described registration. Preserve the Core-only consumer gate.

No packaging rewrite, extra target, speculative folder hierarchy, generalized registry, or new dependency is warranted by this review.

## Documentation and product corrections

### D1 — Correct the file-replacement failure promise

[`docs/EVENT_FORMAT_V1.md:11`](EVENT_FORMAT_V1.md#L11) says any retention failure preserves the prior bytes and generation. [`FileEventStore.cpp:163`](../src/events/FileEventStore.cpp#L163) installs the replacement and updates its state before the post-replacement injection and parent-directory flush; failures there fault the store. [`test_event_store.cpp:487`](../tests/test_event_store.cpp#L487) deliberately tests that behavior.

Describe the actual commit boundary: failures before replacement preserve the original; after replacement, an error can leave the replacement installed and durability uncertain, so the store faults. Preserve the established recovery behavior rather than changing code to fit the stronger prose. This is a confirmed documentation mismatch, not a newly discovered missing fault-injection test.

### D2 — Separate historical completion evidence from current status

[`COMPLETE_SOLID.md:18`](../COMPLETE_SOLID.md#L18) and line 196 still describe Stage 2 as research/first-milestone definition. Other sections retain present-tense “issues remain” language even though the top declares the old findings closed and the active docs name L0.

Preserve the historical audit, mark the old findings and conclusions as dated, and link their closure evidence. Point current scope to `DEVELOPMENT_TRACKING.md` and the L0 spec. A completion assessment is evidence about a revision, not a permanent assertion that later reviews cannot find defects. Avoid repeating full milestone status and acceptance lists in every overview document.

### Product boundaries and success criteria

- Rename the heading of [`PRODUCT.md`](../PRODUCT.md) to make its Solid Scope scope explicit. It currently describes the owner-operated simulation instrument, not the whole Liquid Layer product.
- Keep the engine proof concrete: intent survival, distinction between desired and observed state, deterministic evaluation, bounded authority, and interchangeable external consumers. These are meaningful achievements already reflected in the architecture.
- Keep reduced cognitive friction as an application hypothesis to evaluate. Before Stage 3 implementation, select one support scenario with intended users, manual override/stop behavior, and observable measures of usefulness, unwanted interventions, correction effort, and user burden. Runtime correctness alone does not demonstrate that benefit.
- Retain the chosen L4 external-agent proof: an external client discovers scoped capabilities, submits a proposal for local validation/evaluation, and inspects bounded results without mutating live World. Hermes remains one consumer, with another client or conformance path guarding against accidental dependence on it.
- Keep transport schemas derived from Liquid semantics. The checked [MCP 2026-07-28 specification](https://modelcontextprotocol.io/specification/2026-07-28) describes stateless requests and per-request negotiation; it does not supply Liquid's authorization or owner-thread rules. Recheck protocol and SDK assumptions when L4 actually starts.
- When specifying L5, require a minimal retained result linking approval, exact proposal/source revision, evaluated scope, and installed behavior revision. L6 can add richer journaling and change feeds. Define the fate of a prior script revision's persistent intents before enabling revision; do not add any of this to L0 or Solid Event Format v1 now.

## L0 direction to retain

The current L0 spec resolves the important semantic questions well. Keep its exact Boolean/Integer/Number/String/Array/Object vocabulary, finite doubles, explicit bounds, distinct read/write schemas, and symmetric convenience path. Preserve literal `{}` as Object and the private marker on host-provided empty arrays. Do not infer formal equivalence between schemas and executable C++ codecs.

For the initial API/header pass, prefer a metadata-bearing `expose_component` overload alongside the existing overload, using the same binding object and pre-execution freeze. Provide a symmetric metadata factory. Build immutable owner-thread manifests from current World permissions, fresh copied readable values, exact host-produced Lua paths, monotonic time, and the independent authoring-contract marker. Do not create a second discovery registry.

Before substantive implementation, make the existing bounds requirements concrete in tests: shared depth/node/byte accounting, aggregate descriptions/enums/capabilities, safe access-expression expansion, and deterministic bounded errors. Validation should walk the bounded in-memory representation directly; there is no need to serialize through JSON merely to validate it.

Use the documented `Light{brightness}` example and a deliberately asymmetric codec. Test actual decoder behavior, readable-snapshot validation, permission revocation/removal, safe unusual names in the real sandbox, immutable copied manifests, and old schema-less execution. Metadata remains description; current closures, permissions, and real decoding remain authority.

## Recommended work order

1. **Contain failures and restore authority/evidence invariants:** B1–B5, each with a regression that fails on the reviewed code. Keep fixes small and separately reviewable. Start with the Lua host abort.
2. **Remove demonstrated waste:** C1 and C3; verify failure semantics and exact evidence compatibility. Bound the cache in C4 before repeated long-lived authoring use. Fix the Scope parser independently.
3. **Correct present claims:** D1/D2, explicit Solid Scope product scope, and C2's finite-session limitation. Record new findings and eventual closure evidence without erasing the historical Solid audit.
4. **Implement only L0:** minimal public headers and tests first, then schema/manifest logic inside the existing Lua boundary. Follow the repository's division of work for substantive core implementation.
5. **Reassess the provisional roadmap from L0 evidence:** retain the selected external-agent priority. Resolve checkpoint longevity before long-running operation, and approval/revision provenance before live activation.

After fixes, run the strict full suite, the relevant sanitizer/concurrency checks, and the consumer-package gates touched by public APIs. Record reproduction and closure at the actual fixed revision. This review creates no implementation milestone approval and does not advance L0 or the later roadmap.

## Closure — 5 September 2026

**Status of this section:** closure evidence recorded after the fixes; the review text above is preserved unchanged as the dated finding record.

The findings were addressed on branch `fix/pre-liquid-hardening`, based on `origin/main` (`af5af08`, whose `src/`, `include/`, `apps/`, and `tests/` trees are identical to the reviewed revision). One commit per finding; the closing commits and regression test names are tabulated in the "Pre-Liquid Hardening — September 2026" section of `DEVELOPMENT_TRACKING.md`.

| Item | Outcome |
| --- | --- |
| B1 | Lifecycle callback lookup, argument construction, and invocation run inside a protected Lua entry; the VM closes through an RAII guard. The 16-change × 2 × 4 KiB reproduction now returns `MemoryLimitExceeded` at 16, 24, 32, 64, and 128 KiB with no committed intents, cancellations, or watches. |
| B2 | Forward/reverse binding indexes change together; feedback projects only through a binding whose current route/target match the incoming key and whose component slot is live. Rebind, internal-control conversion, removal/recreation, stale queued reports, and target reuse are covered. |
| B3 | The shared in-flight entry carries the leader's command and outcome; waiters consume it directly. The overlapping-waiter scenario failed within two iterations on the reviewed code and is stable across repeated runs after the fix. |
| B4 | Behavior and component removals record their tombstone/removed-component evidence whenever the removal completed before a callback threw; pre-removal failures record nothing. |
| B5 | The effects-path catch publishes the failed `FrameLog` before best-effort failure evidence; injected failures before systems, after dispatch, and while recording failure evidence all surface through `last_frame_log()`. |
| B6 | Blank or whitespace-only brightness is rejected; explicit `0` remains valid. The headless self-test exercises the parser table. |
| C1 | Retained-record capacity grows geometrically up to the record cap. Debug probe (2k/4k/8k single appends): 0.498/1.973/7.929 s before, 0.015/0.012/0.022 s after. The simulation-adapter test dropped from about 148 s to 1.35 s in the same build profile. |
| C2 | Documented as a finite-session limitation in `docs/EVENT_FORMAT_V1.md` and pinned by a replay regression (3,000 embedded frame records survive retention; the next checkpoint after 1,000 more fails with `value node limit exceeded`). No Value-limit or format change. |
| C3 | One `Impl` helper emits `ScriptExecuted` evidence for both entry points; parity is tested in full-source and hash-only modes. |
| C4 | The capability cache is bounded at 256 entries with dead-generation pruning and clear-on-overflow; eviction only forces recomputation. |
| C5 | Recorded as L0 fixture guidance in `DEVELOPMENT_TRACKING.md`; the new regressions use public codec registration. |
| D1 | `docs/EVENT_FORMAT_V1.md` now states the atomic-replacement commit boundary. |
| D2 / product | `COMPLETE_SOLID.md` status, Scope findings, and verdict are dated evidence pointing to `DEVELOPMENT_TRACKING.md`; `PRODUCT.md` is explicitly the Solid Scope document. |

Verification at the closing revision (local, Linux, GCC):

- strict Debug build (`LIQUID_ENABLE_STRICT_WARNINGS=ON`, `LIQUID_WARNINGS_AS_ERRORS=ON`): zero warnings;
- `ctest --test-dir build -j4 --output-on-failure`: 30/30 passed, 9.35 s total;
- ASan/UBSan Debug build (`LIQUID_ENABLE_SANITIZERS=ON`), same eight-test subset as the review command above: 8/8 passed; `idempotent_dispatcher` also passed `--repeat until-fail:20` under the sanitizers;
- `visualizer_selftest` (dependency-free Node harness) passed with the new parser table;
- the simulation-adapter test, which took about 148 s at the reviewed revision, completes in about 1.3 s after C1 in the same Debug profile.

Not run locally: ThreadSanitizer, coverage, Release, cross-platform, fuzz, and consumer-package matrices (Clang is not installed on this machine; the standing CI matrix covers TSan, coverage, Release, and Core-only consumers on push). This closure does not create a milestone approval and does not advance L0.
