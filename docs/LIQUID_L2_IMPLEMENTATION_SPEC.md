# L2 — Isolated deterministic proposal evaluation

**Status:** specified; inactive. **Dependency:** L1 accepted and landed.
Apply [the common contract](LIQUID_IMPLEMENTATION_CONTRACT.md).

## Outcome and existing evidence

Evaluate exact submitted source through a fresh real `Runtime`,
`LuaLifecycleSystem`, runner, intent resolver and `InMemoryAdapter`. Never run
candidate source against the live World. A passing result means the registered
finite scenarios passed, not that all future states or human outcomes are safe.

Read `apps/SimulationScenario.cpp` and the simulation adapter tests for setup
and delivery order. That scenario is light-specific and is not an installed
generic evaluator. Reuse the installed InMemoryAdapter and Runtime; do not
depend on the Scope library or copy its simulation loop into another Runtime.

## Additive Solid prerequisite: independent lifecycle sources

Current `LuaLifecycleSystem(type, runner, componentName="lifecycle")` reads the
same globally named script component for every member. Preserve that constructor
and behavior. Add an overload taking `LuaScriptSelection::SingleReadable` as
the third argument, alongside the string overload.

In this mode each behavior must have exactly one readable `LuaBehaviorScript`
component. Select it through existing `World::get_components` and typed reads.
Zero or multiple readable scripts produce a bounded HostError result for that
behavior, execute no source, and do not fault unrelated behaviors. Write-only
script access is not readable. The host never exposes script-control components
to Lua model bindings. A generated behavior receives only Read on its script.

The lifecycle state key includes selected script slot identity/name as well as
source and revision. A changed slot, source or revision resets started/watch
state and the next successful frame runs `on_start`. Removing a behavior clears
its lifecycle cache through the existing membership callback. Do not implicitly
delete intents when a source component is edited; L5 defines managed revision
cleanup. Legacy fixed-name behavior and full bundle rollback stay unchanged.

## Evaluator interfaces and fixture trust

Add `EvaluationFixtureId{stableName, version}`, `EvaluationSuite`,
`PreparedEvaluation`, `EvaluationCaseResult`, `EvaluationRecord` and
`AuthoringSession::evaluate(CallerContext, ProposalId, EvaluationFixtureId)`.
Only the host registers suites before the session accepts proposals. Models
choose from an allowlisted suite ID; they do not submit setup callbacks, adapter
routes, scenario assertions, shell commands or arbitrary fixture data.

A suite contains an ordered vector of cases. Each case has a stable name,
copied private fixture inputs, explicit nondecreasing frame times,
InMemoryAdapter behavior, and trusted expectation callbacks. Each case factory
returns an owned Runtime, a new runner bound to its World, the candidate behavior,
shared InMemoryAdapters, and the candidate's isolated capability manifest.
It registers the same versioned component/Lua codec definitions as the live
host and grants exactly the captured scope. It installs the proposal's exact
source and uses SingleReadable mode. Neither Runtime nor runner is copied.

The host factory is trusted native code; declaration of codec versions is not
a proof that arbitrary C++ callbacks are identical. Require component-schema
identity, binding metadata, permissions and authoring limits to match the
captured scope, and test the factory against live host registration fixtures.
Compare semantic schema/binding/grant definitions, not fresh world handles or
runner instance identities: those deliberately differ in the isolated world.
Production adapters, files, network calls, wall clocks, random input and calls
to `run_frame` inside factories/expectations are forbidden by this fixture
contract. The evaluator alone drives the prepared Runtime's frames.

Initial fixture state is explicit and frozen per evaluation, including private
Write-only target baselines. It need not equal current live values. Record the
fixture identity and version so reviewers know what was tested. Never obtain a
whole-World clone or generic snapshot restoration by reaching into internals.

## Evaluation algorithm and evidence

1. Resolve the immutable proposal, check caller/scope and all stale identities.
2. Capture the chosen suite and inputs once; admit all case/record budgets.
3. For each case, prepare a fresh isolated world with the exact candidate scope.
4. Before each explicit frame time, apply only that case's registered input
   actions and call `deliver_through(now)` on its in-memory adapters. Run one
   real `Runtime::run_frame(FrameInput)`; no sleep or wall-clock progression.
5. Read lifecycle result, frame/effect results and the test event store; run
   host assertions. Stop the case on first execution, assertion or bounds failure.
6. Produce one immutable EvaluationRecord bound to the proposal and fixture.

No externally visible live state changes during any phase. Cases after a failed
case are marked `NotRun`; they never count as passed. Evaluation status is
`Passed` only if every required case and assertion passed. Other statuses are
`Failed`, `LimitExceeded`, `HostError`; a result retains completed case evidence
with explicit completeness, never a partial success. Invocation failure before
admission returns an AuthoringError and creates no evaluation record.

Use a src-private bounded EventStore wrapper over MemoryEventStore to enforce
both common record-count and logical-byte limits before append. This is disposable
simulation evidence, not a new durable store. Budget exhaustion faults/ends
that evaluation and preserves the live world. No retention loop is attempted.

The host-private record holds exact source identity, scope capture, suite/version,
frozen inputs, frame times, assertions and bounded Solid records. Public results
contain scoped summaries separating proposal acceptance, live intents, selections,
issued commands, reports, observations and authoritative final state. Receiving
an Applied report is not itself proof that Runtime accepted/projected it.
Write-only initial values and hidden competitor details are never returned.

Repeated runs compare normalized semantic traces: map fresh world/behavior/intent
handles to first-seen local ordinals, retain relative ordering, types, exact
values, explicit times and outcomes. Raw handles differ across fresh worlds;
do not claim byte-identical raw event streams. Normalization never removes a
failure, selection change, source difference or adapter outcome.

## Required fixture suite

Use host-owned `lighting-v1` fixtures with Light brightness 0..100 and an
integer internal-state component. Cases cover: persistent 70 success; competing
higher-priority 0 then cancellation/reselection of the original 70 intent;
until-time expiration; internal-state persistence across fresh VMs; deferred
feedback; rejected/silent/duplicate/delayed reports; and no model connectivity.
Explicit frame schedules are `{0, 10, 20}` except timeout/expiry cases, whose
named fixture data declares the tested deadline and a frame immediately after it.

Separate negative proposals cover syntax/runtime errors, forbidden access,
invalid codec shape, failed cancellation/replacement bundle, instruction/memory
limits and unknown fixture IDs. Expectations are host code, not model-written
assertions. Include two independent Lua behaviors with different script slots.

## Implementation steps and tests

| Step | Implement after its failing test | Required oracle |
| --- | --- | --- |
| L2.1 | SingleReadable lifecycle overload | Two different script sources execute independently; zero/multiple/read-only cases; slot replacement resets watches; legacy constructor unchanged |
| L2.2 | Trusted suite registration and isolated preparation | Live World counts/data/intents/effects unchanged, exact grants, no production adapter, fixture mismatch rejected |
| L2.3 | Bounded evaluation loop and records | Real lifecycle/codec/rollback, all required cases, explicit NotRun/failure, event-store budget and callback exception containment |
| L2.4 | Repeatability and package evidence | Two fresh runs have equal normalized traces; changed input changes the expected outcome; no Scope dependency; installed Authoring evaluation works |

Register `authoring_evaluation`; extend existing lifecycle tests. Run
`ctest --test-dir build/strict -R '^(authoring_evaluation|lua_lifecycle)$' --output-on-failure`
and the common full gates. L2.1 requires actual behavior-level red/green evidence
against the fixed-name-only behavior, not just a compilation test.

## Allowed files and exit

New: `include/liquid/authoring/Evaluation.hpp`, `src/authoring/Evaluation.cpp`,
`src/authoring/BoundedEvaluationStore.hpp`, `tests/test_authoring_evaluation.cpp`.
Existing: session/types, lifecycle header/implementation and tests, Authoring
consumer, CMake and affected docs. Fixtures stay in the focused test file;
do not generalize or move Scope's light-specific scenario as part of L2.

Exit: external source can be evaluated through the real deterministic path,
with independent scripts, faithful failures and bounded scoped evidence.
Approval, live installation, generic remote execution and physical safety
claims remain out of scope.
