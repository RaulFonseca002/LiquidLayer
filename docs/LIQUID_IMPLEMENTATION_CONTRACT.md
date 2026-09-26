# Liquid Common Implementation Contract

**Status:** proposed additions for L0–L6; no implementation activation.
Read [the roadmap](LIQUID_STAGE2_PLAN.md), the active spec, and
[the handoff workflow](HERMES_MULTI_MODEL_DEVELOPMENT_GUIDE.md).

## Ownership, authority and identity

All C++ authoring session calls run on the World/Runtime owner thread, between
frames. Trusted host callbacks must be bounded, deterministic for given inputs,
non-reentrant and observational unless their API explicitly prepares or changes
topology. This is an in-process trusted-host contract, not a hostile-native-code
sandbox. Model work and transport waits never run inside a frame.

`liquid::authoring::AuthoringSession` holds references to one live Runtime and
its configured Lua runner and registered `ComponentType<LuaBehaviorScript>`;
the Runtime and runner outlive it. The script type is passed explicitly, not
guessed from a schema name, and cannot be included in an authoring scope.
The session owns scopes, proposals,
evaluations, managed-behavior mappings and, from L4, its journal. It does not own
frame progression, duplicate component state, or predict resolver results.

Introduce strong IDs `ScopeId`, `ProposalId`, `EvaluationId`, `ManagedBehaviorId`,
`ApprovalId`, `OperationId`, and `JournalSequence`. They are nonzero uint64
monotonic counters, never recycled within an `AuthoringSessionId` supplied by
the trusted host as a 32-character lowercase hexadecimal identity, unique
across that host's sessions. Exhaustion is an error, never wraparound. The host must use a
new session identity after restart. Cross-session references fail. IDs are
locators, not bearer credentials or cryptographic secrets.

Every external-facing call receives a trusted `CallerContext` bound by the
host/transport to one scope owner; the caller cannot submit this identity as a
tool argument. Every record lookup checks session and scope ownership. Approval
and live operations are separate host-only APIs, absent from model transport.
The record repository returns immutable copies or const views; no caller can
replace source or grants through an ID.

## Public result and value conventions

L1 adds `AuthoringErrorCode`: `InvalidInput`, `NotFound`, `AccessDenied`,
`StaleScope`, `StaleApproval`, `Unsupported`, `LimitExceeded`, `EvaluationFailed`,
`HostError`, `NeedsIntervention`. Public authoring calls return
`AuthoringResult<T>` containing exactly one value or `AuthoringError` with code,
bounded diagnostic and optional field path. Empty success and failure are
distinct. Missing and unauthorized external record lookups both render
`NotFound` to avoid enumeration. Native host diagnostics may retain the cause.
L0 schema configuration uses `std::invalid_argument`; freeze violations use
`std::logic_error`; manifest/validation calls use their specified result types.

Admission checks occur before allocation, recursion or callbacks where input
sizes are known. Catch host exceptions at public operation boundaries; an
unrecoverable allocation failure may propagate to the trusted host, never be
reported as a successful model result. No result depends on `exception.what()`
for machine interpretation. Unknown fields and duplicate logical keys fail.

Use exact `LuaValue` kinds, never a JSON-number approximation. Native records
hold copied typed values. L6's wire encoding carries signed integers and finite
doubles as distinct tagged decimal strings; byte strings and names use base64.
The proposal contains exact Lua UTF-8 source bytes, never normalized or reprinted
after review. Reject NUL in source and invalid UTF-8 at proposal submission.
Existing trusted schema-less Lua execution is unaffected by this new admission.

## Default finite limits

These defaults are part of the proposed contract, not measurements of current
Solid capacity. Tests use smaller configured limits to prove boundaries.
Host configuration may lower limits; raising them requires a spec change.

| Authoring resource | Maximum |
| --- | --- |
| Active scopes / proposals / evaluations / managed behaviors | 64 / 128 / 256 / 64 per session |
| Source / proposal rationale | 65,536 / 2,048 bytes |
| Diagnostic | 4,096 bytes, including field path |
| Scope capabilities | 128 |
| Label or semantic identity string | 256 bytes |
| Copied manifest / view / evaluation response | 1 MiB logical payload each |
| Evaluation cases / frames per case | 16 / 256 |
| Evaluation records | 8,192 records and 8 MiB logical payload per run |
| Journal | 4,096 records and 16 MiB logical payload per session |
| Journal page | 128 records and 1 MiB |
| Approvals / operations | 128 / 256 per session |
| All session-owned artifact payloads together | 64 MiB |
| L6 input or output line | 4 MiB UTF-8 including terminating newline |

Logical payload accounting is deterministic: 8 bytes per numeric scalar,
1 per boolean, byte length for strings, and the sum of child payloads plus
8 bytes per collection element/field and key bytes for maps. Schemas add the
same accounting for their tagged fields. IDs count as 8-byte scalars. Count all
duplicated output data each time; shared allocation is not a budget discount.
This bounds logical data; allocator overhead is not an exact RSS guarantee.
L0 additionally bounds depth/nodes and construction bytes in its spec.

Collections that convey authority or evaluation outcomes fail completely on
overflow. Do not truncate a manifest, required case, winner set or error code.
Only human diagnostics truncate, retaining a terminal `...` inside the budget.
Journal pagination is explicit. There is no silent eviction of approval inputs.
The session-wide ceiling applies from L1, including private evaluation traces
from L2; L4 journal storage also counts against it. Reserve the maximum admitted
evaluation payload before execution, then release unused capacity on completion.
All host times passed to one session are nondecreasing and within the runner's
integer time range. Backward time is InvalidInput and never advances counters.
Calls without an explicit `now` use the last accepted host session time for
journal stamping. This is a host observation time, not a wall-clock completion
timestamp. Isolated evaluation frame times never advance the live session clock.

## Exact approvals and staleness

An immutable proposal binds exact source, scope ID/version, private captured
target identities, authoring/binding contract version, and optional expected
managed-behavior revision. Scope targets use live world-bound generational
handles privately; public output has semantic names and opaque references.

Before evaluation and again before approval/activation, re-resolve targets and
compare identities, grants, host policy revision, runner binding identity and
expected managed revision. Removal/recreation under the same name is stale.
Scope revisions are monotonic and updated only through session APIs. Revoke
invalidates dependent approvals immediately. Live state values are not access
authority: changing values does not by itself invalidate a scope.

Evaluation uses a frozen scenario snapshot, not a proof that arbitrary future
live state is equivalent. L5 additionally requires an explicit host validity
predicate over current live context and a monotonic approval deadline. A model
cannot choose either predicate or deadline. No automatic retries or permission
widening follow rejection.

Do not use Solid's `fnv1a64:` diagnostic source identifier as authorization.
Approval references the host's immutable proposal/evaluation records and compares
the complete bound identity and source before operation. A hash may be displayed
for diagnostics, but matching a hash alone never authorizes installation.

## Packaging and allowed-file rules

L0 extends existing `Liquid::Lua`. L1 creates `Liquid::Authoring`, namespace
`liquid::authoring`, with `LIQUID_BUILD_AUTHORING=OFF` by default. Enabling it
requires Lua and Simulation; invalid combinations fail configure explicitly.
Authoring links existing `Liquid::Core`, `Liquid::Lua`, `Liquid::Simulation`.
No dependency cycle: neither Lua nor Simulation links Authoring.

Add `Authoring` as an optional `find_package` component with its own export
file and dependency checks. Preserve the existing component set when disabled;
update exact-export tests to distinguish these two configurations. L1's
allowlist includes `CMakeLists.txt`, `cmake/LiquidConfig.cmake.in`,
`cmake/README.md`, existing package/consumer tests and examples. No global
package version bump or release is part of a milestone step.

Each spec names future source files. They are not created during documentation
validation. Implementation may modify those files, its focused tests, CMake
wiring, existing installed consumers, and the affected existing documentation.
Anything else needs an explicit spec/allowlist correction first.

## Required commands and gates

Run from the isolated implementation checkout. Use distinct build directories.
The exact compiler versions and dependency locks go in the evidence packet.

Baseline / L0 (Authoring does not exist yet):

```sh
cmake -S . -B build/strict -DCMAKE_BUILD_TYPE=Release -DLIQUID_ENABLE_STRICT_WARNINGS=ON -DLIQUID_WARNINGS_AS_ERRORS=ON
cmake --build build/strict --parallel 2
ctest --test-dir build/strict --output-on-failure
```

From L1, add `-DLIQUID_BUILD_AUTHORING=ON` to configure. Every implementation
step runs its spec's focused CTest selection and the full strict suite. Confirm
the regex selects at least one test; zero tests is failure, not evidence.

At every milestone boundary run strict GCC and Clang builds, plus:

```sh
cmake -S . -B build/asan -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DLIQUID_ENABLE_STRICT_WARNINGS=ON -DLIQUID_WARNINGS_AS_ERRORS=ON -DLIQUID_ENABLE_SANITIZERS=ON
cmake --build build/asan --parallel 2
ctest --test-dir build/asan --output-on-failure
cmake -S . -B build/tsan -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DLIQUID_ENABLE_STRICT_WARNINGS=ON -DLIQUID_WARNINGS_AS_ERRORS=ON -DLIQUID_ENABLE_THREAD_SANITIZER=ON
cmake --build build/tsan --parallel 2
ctest --test-dir build/tsan --output-on-failure
```

From L1, enable Authoring in both sanitizer configurations too. Never combine
ASan/UBSan, TSan and coverage in one build. An unavailable sanitizer environment
is recorded as unverified and blocks that gate until a supported run exists.

Core-only and coverage, with Authoring left disabled:

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release -DLIQUID_BUILD_LUA=OFF -DLIQUID_BUILD_SIMULATION=OFF -DLIQUID_ENABLE_STRICT_WARNINGS=ON -DLIQUID_WARNINGS_AS_ERRORS=ON
cmake --build build/core --parallel 2
ctest --test-dir build/core --output-on-failure
cmake -S . -B build/coverage -DCMAKE_BUILD_TYPE=Debug -DLIQUID_BUILD_LUA=OFF -DLIQUID_BUILD_SIMULATION=OFF -DLIQUID_ENABLE_COVERAGE=ON -DLIQUID_ENABLE_STRICT_WARNINGS=ON -DLIQUID_WARNINGS_AS_ERRORS=ON
cmake --build build/coverage --target coverage --parallel 2
```

Use the coverage toolchain/dependency versions from `.github/workflows/ci.yml`.
Do not silently lower its 90% line / 80% branch thresholds. Verify full and
Core-only source/installed consumers using the existing example projects:

```sh
cmake --install build/strict --prefix "$PWD/build/prefix"
cmake -S examples/installed-package -B build/installed-consumer -DCMAKE_PREFIX_PATH="$PWD/build/prefix"
cmake --build build/installed-consumer --parallel 2
ctest --test-dir build/installed-consumer --output-on-failure
cmake -S examples/source-tree -B build/source-consumer -DLIQUID_SOURCE_DIR="$PWD"
cmake --build build/source-consumer --parallel 2
ctest --test-dir build/source-consumer --output-on-failure
git diff --check
```

L1 adds a separate installed Authoring consumer CTest alongside existing
consumers; it explicitly requests `COMPONENTS Authoring`. Keep the old consumers
unchanged in their default configuration. Run the existing CI Core-only consumer
commands as well. L6 adds its protocol tests; engine CI must remain runnable
without Python SDK/network dependencies when L6 tooling is disabled.

## Evidence and approval

Each step records: base/spec revision, changed-file diff, required behavior,
focused failure before implementation, focused/full success after it, and
unverified checks. Missing APIs may initially cause a compile failure; once the
API exists, prove the behavioral assertion fails against a temporary stub or
pre-fix implementation in a disposable checkout, never by stashing other work.

Codex checks the implementation against contracts and evidence, not just test
totals. It returns findings or `no blocking findings`, never an owner approval.
Only the owner activates the next step. Baseline tests cannot prove future
Liquid behavior, and simulation cannot prove human suitability.
