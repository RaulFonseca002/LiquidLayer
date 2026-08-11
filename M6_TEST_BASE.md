# M6 Simulation CLI Test Base

**Status:** Implemented and under final M6 owner acceptance  
**Date:** August 11, 2026  
**Scope:** Stage 1, Solid — M6 only

## 1. Testing Position

M6 is the first executable demonstration of a user-directed Solid scenario, but it is not yet a study of human outcomes.

The CLI may prove that:

- an explicit user preference or override is represented as deterministic scenario input;
- the existing Lua boundary turns that input into controlled intent proposals;
- the existing Runtime selects intents reproducibly at explicit frame times;
- successful results and bounded failures are inspectable;
- the same scenario produces the same observable output when replayed.

The CLI must not claim that one lighting value, reminder, routine, or environmental response is correct for all neurodivergent people. Research consistently points toward individual differences, context, control, personalization, and predictability. In M6, the represented user chooses the scenario values; Solid evaluates them but does not infer the user's internal state.

Two actors must remain distinct in every test:

1. **Scenario operator:** the developer or researcher invoking the CLI.
2. **Represented user:** the person whose explicit preference is encoded in the initial state and Lua source.

This distinction prevents a deterministic engine test from being mistaken for usability or clinical validation.

## 2. Evidence Applied to the Test Design

The test base uses the following research conclusions:

- Human-centered design begins with explicit users, tasks, and environments, then iterates through user-centered evaluation. A simulator is useful before participant testing, but does not replace it ([NIST Human Centered Design](https://www.nist.gov/itl/iad/human-centered-technologies/human-factors-human-centered-design)).
- Cognitive accessibility benefits from personalization and user control over when content or state changes ([W3C COGA: Support Adaptation and Personalization](https://www.w3.org/WAI/WCAG2/supplemental/objectives/o8-personalization/)).
- Predictability and help avoiding or correcting mistakes are accessibility principles, while significant changes should not occur without user consent ([W3C Accessibility Principles](https://www.w3.org/WAI/fundamentals/accessibility-principles/)).
- Qualitative studies of autistic adults identify sensory elements, routines, person-environment interaction, and control as important but varied factors in the home ([Exploring the home environment of adults living with autism spectrum disorder](https://pubmed.ncbi.nlm.nih.gov/38481453/)).
- Autistic adults' accounts emphasize control over sensory stimuli and show that sensory experiences can be positive or negative depending on the person and context ([The sensory experiences of adults with autism spectrum disorder](https://pubmed.ncbi.nlm.nih.gov/26422904/)).
- Participatory research identifies predictability, adjustments, and recovery among the principles that can make sensory environments enabling or disabling ([Sensory Experiences of Autistic Adults in Public Spaces](https://pubmed.ncbi.nlm.nih.gov/38116051/)).
- Smart-home control preferences vary across place, time, control mechanism, and device failure; tests should therefore include explicit override and failure cases rather than assume one automation mode ([Control in Context](https://doi.org/10.1145/3772318.3790549)).
- Adults with ADHD report varied compensatory strategies, including reminders, calendars, and other technology; this supports configurable scenarios rather than a universal reminder behavior ([Skills and compensation strategies in adult ADHD](https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0184964)).

These sources guide scenario construction only. Their samples and methods do not establish a universally preferred environmental response.

## 3. Implemented Minimal User Scenario

Use one intentionally narrow lighting scenario for M6:

- A named component `Light.officeLight` starts at an explicit brightness.
- A represented user has write access to that light.
- Lua explicitly proposes a temporary high-priority user preference and may also propose a persistent fallback.
- The Runtime executes frames at caller-provided monotonic times.
- Resolution reports the selected intent for `officeLight` without pretending that the physical light was changed.

Lighting is useful here because it is inspectable and already exercised by the M5 codec tests. It is an engine demonstration, not a recommendation about ideal brightness.

The smallest successful two-frame scenario is:

```text
initial brightness: 10
frame times: 100 ms, 105 ms
proposal A: brightness 30, low priority, persistent
proposal B: brightness 70, high priority, duration 5 ms

frame 0 at 100 ms: proposal B is selected
frame 1 at 105 ms: proposal B is expired and proposal A is selected
```

This one scenario demonstrates explicit input, user control, multiple proposals, priority, lifetime, expiration, frame time, deterministic selection, and replay.

## 4. Implemented CLI Contract

The M6 CLI accepts only the inputs the milestone needs:

```text
liquid_sim_cli \
    --initial-brightness <0..100> \
    --script <lua-file> \
    --frame-time <milliseconds> [--frame-time <milliseconds> ...]
```

Implemented rules:

- Require one initial brightness, one script path, and at least one frame time.
- Interpret every time as monotonic session-relative milliseconds, never wall-clock time.
- Preserve repeated `--frame-time` values in command-line order.
- Accept equal consecutive frame times because Runtime already permits them.
- Reject decreasing times before or through the Runtime without faulting an already-valid run.
- Read Lua source as bytes and let `LuaBehaviorRunner` enforce its source limit.
- Do not add a general scenario language, JSON input parser, random seed, event system, or device adapter in M6.
- Provide `--help`; invalid or incomplete invocations write a bounded diagnostic to standard error and return nonzero, consistent with conventional utility behavior ([POSIX utility conventions](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap01.html)).

### Exit status contract

| Status | Meaning |
|---:|---|
| `0` | Every requested frame completed and the Lua execution succeeded. |
| `2` | CLI usage or scenario input was invalid. |
| `3` | Lua returned a bounded non-success status; Runtime remained healthy. |
| `4` | Runtime or host integration failed. |

The exact numeric values are an M6 CLI contract, not a mapping of the internal enum values.

### Output contract

Use deterministic, line-oriented printable ASCII with fixed field order for M6. Bytes outside printable ASCII in diagnostics are escaped as `\\xNN`. Human diagnostics go to standard error and inspectable scenario records go to standard output.

Each run reports:

- normalized initial state;
- Lua status, bounded diagnostic, and created intent IDs;
- each frame number and explicit time;
- completion state and phase counts;
- expired intent count;
- requested and selected intent counts;
- every selected component type, component name, and intent ID;
- selected typed value, priority, and lifetime when available;
- final Runtime health.

Do not include wall-clock timestamps, pointer values, absolute build paths, elapsed durations, locale-formatted numbers, unordered-container iteration, or nondeterministic seeds in golden output.

If structured JSON output is added after M6, use unique property names and a deterministic serialization rule. JSON itself does not give semantic significance to object-member order ([RFC 8259](https://datatracker.ietf.org/doc/html/rfc8259)); canonical JSON requires additional constraints such as deterministic property sorting ([RFC 8785](https://datatracker.ietf.org/doc/html/rfc8785)).

## 5. Required End-to-End Golden Scenarios

### G1 — Explicit preference succeeds

**Actor:** scenario operator representing a user-selected lighting preference.  
**Input:** valid initial brightness, valid Lua file, frames at `100` and `105`.  
**Action:** run the CLI once.  
**Expected:**

- exit status is `0`;
- both frames complete;
- Runtime is not faulted;
- the Lua status is `Success`;
- two intents are created for the host-selected behavior and authorized light;
- the high-priority temporary intent is selected at `100`;
- it expires at `105`;
- the persistent fallback is selected at `105`;
- the original component value remains unchanged because M6 resolves but does not apply intents;
- stdout matches the checked-in golden expectation byte for byte;
- stderr is empty.

### G2 — Script failure is bounded

**Actor:** scenario operator supplying a faulty user behavior script.  
**Input:** valid initial state and times; Lua source `error("script failure")`.  
**Action:** run the CLI once.  
**Expected:**

- exit status is `3`;
- the Runtime frame still completes;
- Runtime is not faulted;
- the Lua status is `RuntimeError`;
- the diagnostic is present and no longer than the configured bound;
- no intent from this execution remains;
- other registered systems still run;
- stdout and stderr match their stable golden expectations.

These are the mandatory M6 CLI regressions and are implemented both in-process and across fresh executable processes.

## 6. Extended Scenario Corpus

| ID | Scenario | Expected invariant |
|---|---|---|
| C01 | `--help` | Exit `0`, usage on stdout, no world constructed. |
| C02 | No arguments | Exit `2`, bounded usage diagnostic on stderr. |
| C03 | Unknown option | Exit `2`; the option is named safely, with no crash. |
| C04 | Missing option value | Exit `2`; no partial scenario executes. |
| C05 | Missing/unreadable script | Exit `2`; no frame executes. |
| C06 | Empty script | Frame completes with Lua `Success`, zero created/selected intents. |
| C07 | Syntax error | Lua `SyntaxError`, bounded diagnostic, zero committed intents. |
| C08 | Runtime error after proposal | Lua `RuntimeError`; buffered proposals are not committed. |
| C09 | Invalid brightness | Lua `InvalidProposal`; no intent is committed. |
| C10 | Read-only capability proposal attempt | Script cannot obtain proposal authority. |
| C11 | Two proposals, different priorities | Highest priority wins deterministically. |
| C12 | Equal-priority proposals | Existing highest-`IntentId` rule wins; do not call this “latest.” |
| C13 | Temporary plus persistent proposal | Temporary wins before expiration; fallback wins at expiration. |
| C14 | Equal frame times | Both frames are accepted and numbered deterministically. |
| C15 | Decreasing frame time | Rejected; invalid input does not fault Runtime. |
| C16 | Source over configured limit | `SourceLimitExceeded`; diagnostic remains bounded. |
| C17 | Infinite Lua loop | `InstructionLimitExceeded`; process completes within test timeout. |
| C18 | Lua allocation pressure | `MemoryLimitExceeded`; process survives and rolls back. |
| C19 | Proposal-count overflow | `IntentLimitExceeded`; no partial execution-owned intents remain. |
| C20 | Non-ASCII script path | Either works as documented or fails cleanly; never corrupts output. |
| C21 | Script path beginning with `-` | Supported through `--` or rejected with an explicit contract. |
| C22 | Maximum representable valid time | No signed-conversion overflow; behavior matches Runtime contract. |
| C23 | Time outside accepted representation | Rejected before frame execution. |

Cases already proved directly by M1–M5 unit tests should remain there. M6 cases prove that the executable preserves those contracts at the composition boundary.

## 7. Deterministic Properties

Golden examples are necessary but insufficient. The M6 tests should assert these properties:

### P1 — Replay identity

For the same executable, arguments, script bytes, and environment-independent inputs:

```text
exit_status(A) == exit_status(B)
stdout(A) == stdout(B)
stderr(A) == stderr(B)
```

Run the golden success and failure scenarios repeatedly in fresh processes and compare bytes or cryptographic hashes.

### P2 — Explicit-time causality

Changing only a frame time may change expiration and selection, but must not change initial state, permissions, script source, or unrelated output.

### P3 — Authority confinement

Every created intent belongs to the host-selected behavior and targets a component for which it currently has write access. Script text cannot choose raw owners, types, or slots.

### P4 — Transactional script failure

Any non-success Lua execution leaves zero intents created by that execution, while older intents remain unchanged.

### P5 — Resolution-only behavior

Selected intent values are observable, but component state is unchanged until a future milestone explicitly adds application/effects.

### P6 — Bounded failure

Malformed, hostile, or excessive Lua source returns one documented status, a bounded diagnostic, and control to the CLI within the test timeout.

### P7 — Registration-order stability

System execution order and frame phase order remain stable. A Lua error result does not stop later systems because it is not thrown through `System::run`.

### P8 — Fresh-process independence

One CLI invocation cannot affect the next invocation through cached authority, global mutable scenario state, wall-clock time, or randomness.

Property-based testing is valuable here even without adding a new framework: generate inputs from a fixed `std::mt19937` seed, assert properties, and print the seed plus minimized case data on failure. Any random test must remain exactly reproducible.

## 8. Stress Strategy

M6 stress testing should complement, not duplicate, the existing manager stress tests and Lua limit unit tests.

### Tier A — Per-commit deterministic stress

Keep this fast enough for normal CTest runs:

- replay G1 and G2 in fresh processes at least 20 times and compare outputs;
- run at least 1,000 explicit frames in-process with nondecreasing fixed-seed times;
- alternate temporary and persistent proposals to exercise creation, expiration, resolution, and ID recycling;
- periodically execute a bounded failing script and prove that later frames still run;
- assert intent counts against a small reference model after every frame;
- assert that borrowed component pointers are never retained across structural mutations;
- set a CTest timeout as a deadlock/infinite-loop ceiling, not as a performance benchmark.

### Tier B — Sanitizer stress

Run the existing and M6 stress paths with AddressSanitizer and UndefinedBehaviorSanitizer. ASan detects memory safety errors, while UBSan detects classes such as invalid shifts, misaligned access, and signed overflow ([Clang AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html), [Clang UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)).

Recommended CI/manual checks:

```text
ctest --test-dir build_sanitized --output-on-failure
ctest --test-dir build --repeat until-fail:20 -R simulation_cli
```

CTest provides repeat-until-fail and timeout controls specifically useful for exposing sporadic failures ([CTest documentation](https://cmake.org/cmake/help/latest/manual/ctest.1.html)).

### Tier C — Longer soak

Run manually or in scheduled CI:

- 100,000 frames with a recorded seed;
- repeated create/destroy cycles for worlds, behaviors, runners, and scripts;
- valid, invalid, and boundary-sized Lua sources;
- stable memory use after warm-up;
- identical result hashes across two complete runs with the same seed.

Do not place fragile wall-clock performance thresholds in the ordinary regression suite. Record throughput and peak memory for observation, but fail only on correctness, sanitizer findings, resource-limit violations, timeout, or a large explicitly approved regression.

### Tier D — Fuzzing after the M6 contract is stable

A narrow fuzz target is appropriate later for the CLI parser or scenario-input decoder. LLVM recommends deterministic, fast, narrow targets that tolerate malformed input and use a small corpus of valid and invalid seeds; combining fuzzing with sanitizers improves defect detection ([LLVM libFuzzer](https://llvm.org/docs/LibFuzzer.html)).

Do not add a fuzzer, corpus folder, or compiler-specific build target during the first M6 slice. The current milestone allows only the small CLI and focused tests, and the parser contract should stabilize first.

## 9. Coverage Matrix

| Contract | Unit/regression | CLI golden | Fixed-seed stress | Sanitizers |
|---|:---:|:---:|:---:|:---:|
| Argument validation |  | Yes | Yes | Yes |
| Explicit initial state | Existing World | Yes | Yes | Yes |
| Lua capability confinement | Existing M5 | Yes | Yes | Yes |
| Lua transactional rollback | Existing M5 | Yes | Yes | Yes |
| Same-frame proposal selection | Existing M4/M5 | Yes | Yes | Yes |
| Expiration and fallback | Existing M2–M5 | Yes | Yes | Yes |
| Stable observable output |  | Yes | Yes |  |
| Replay byte identity |  | Yes | Yes |  |
| Runtime fault semantics | Existing M4 | Yes | Yes | Yes |
| ID/storage recycling | Existing stress |  | Yes | Yes |
| Resource bounds | Existing M5 | Selected cases | Yes | Yes |

## 10. Implementation Record

1. The CLI invocation, exit statuses, and output fields are frozen by `tests/test_simulation_cli.cpp`.
2. CMake builds `liquid_sim_cli` and runs a direct fresh-process regression with a hard timeout.
3. G1 and G2 use the existing Runtime and the one-shot Lua-system pattern; G2 proposes before failing to prove transactional rollback.
4. Operator-facing validation covers missing, duplicate, unknown, malformed, out-of-range, and decreasing inputs.
5. Both golden scenarios replay byte-for-byte across 20 fresh processes.
6. `tests/test_stress.cpp` runs 1,000 fixed-seed Runtime/Lua frames against a small reference model, including failures and persistent fallback.
7. The complete M1–M6 suite is run in normal, strict-warning, and sanitizer builds before handoff.

### Build and run

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/liquid_sim_cli --help
./build/liquid_sim_cli --initial-brightness 10 --script scenario.lua --frame-time 100 --frame-time 105
```

The script file is local input. The CLI reads at most the configured Lua source limit plus one byte so the existing Lua boundary remains responsible for reporting `source_limit_exceeded`.

## 11. M6 Acceptance Gate

M6 is ready to call complete only when all of the following hold:

- G1 and G2 pass end to end.
- Replaying either golden scenario produces byte-identical observable output.
- A script error is visible and bounded without faulting Runtime.
- Explicit times control expiration and selection exactly as documented.
- No physical action or component mutation is implied by resolution output.
- Invalid CLI input fails before partial scenario execution.
- M1–M5 tests remain green.
- Fixed-seed stress passes repeatedly.
- Sanitizer-backed tests pass.
- The CLI documentation states what was simulated and what was not validated about real users.
