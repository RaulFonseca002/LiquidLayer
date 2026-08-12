# Complete Solid

## Status

This is the provisional working document for finishing the Solid stage of Liquid.

M1 through M6 establish the deterministic world, components, behaviors, intents, lifetime and resolution, frame execution, Lua capability boundary, and simulation CLI. They do not yet prove every end-to-end responsibility required by a complete Solid runtime.

Before beginning the adaptive Liquid stage, the project will make a full pass through the codebase and architecture. That pass will identify missing contracts, partially implemented flows, and tests required for a genuinely complete Solid state. Findings will be recorded here before they are divided into implementation milestones.

This document is not yet the final milestone plan.

## First Confirmed Gap: Effect Application and Device Feedback

The current runtime completes this path:

```text
behavior or Lua script
    -> immutable intent proposal
    -> deterministic intent resolution
    -> selected intent handle
    -> observable frame result
```

It does not yet complete this path:

```text
selected intent
    -> resolved effect
    -> adapter command
    -> device or simulator result
    -> validated runtime input
    -> observed component-state update
```

Intent resolution must remain separate from effect application. Selecting a desired value does not prove that a physical device applied it. The component representing observed state must change only when the application boundary has produced an authoritative simulated result or received a device report.

The missing boundary is required even before production hardware integration. A simulated device should implement the same command-and-feedback contract that a future physical adapter will implement.

## Required Contract

The final names and types will be selected during the full design pass, but the contract must represent at least:

- a resolved effect derived from a selected intent;
- a stable command identity suitable for correlating asynchronous results;
- the target component and requested value or operation;
- dispatch to a simulation or device adapter without exposing `World` internals;
- a bounded ingress path for adapters to report applied, rejected, failed, or pending work;
- an observed value when the device can report one;
- deterministic validation and application of accepted reports on the Runtime-owning thread;
- protection against duplicate, stale, out-of-order, or mismatched reports;
- inspectable frame, command, result, and diagnostic evidence;
- behavior suitable for deterministic replay and stress testing.

Possible conceptual operations are:

```text
dispatch(command)
report_applied(command_id, observed_state)
report_rejected(command_id, reason)
report_failed(command_id, reason)
```

These names are illustrative, not an approved public API.

## Ownership and Safety Invariants

- `IntentRegistry` selects intents; it does not mutate component state or perform I/O.
- A selected intent remains a desire until an effect/application boundary handles it.
- Adapters never receive `World`, registries, component pointers, or unrestricted mutation authority.
- External callbacks do not mutate `World` directly. `World` and `Runtime` remain single-thread-confined.
- Adapter reports enter through a bounded handoff and are consumed by the Runtime on its owning thread.
- Component state distinguishes the requested value from the last authoritative observed value.
- A failed or timed-out command does not silently change observed component state.
- Simulation and physical adapters share the same semantic contract.
- All ordering, identifiers, errors, and state transitions needed for replay are explicit.

## Initial Simulation Acceptance Scenario

Given `Light.officeLight` with observed brightness `10`:

1. Lua proposes persistent brightness `30` at low priority.
2. Lua proposes brightness `70` at high priority for 5 milliseconds.
3. At simulation time `100`, resolution selects `70`.
4. The simulated adapter receives the corresponding command.
5. The simulator reports that brightness `70` was applied.
6. Solid validates the report and updates observed brightness to `70`.
7. At simulation time `105`, the temporary intent expires and resolution selects `30`.
8. The simulator reports that brightness `30` was applied.
9. Solid updates observed brightness to `30`.

The trace must keep the following evidence distinct:

- proposed intents;
- selected desire;
- dispatched command;
- device or simulator result;
- observed component state.

Failure variants must prove that rejected, failed, missing, duplicated, stale, and out-of-order reports do not create false component state.

## Full Solid Completion Pass

The future audit will examine the whole codebase and canonical architecture rather than assuming the device-feedback gap is the only missing work.

### Pass 1: Inventory

- Map every public type, registry, runtime phase, scripting boundary, CLI boundary, and state owner.
- Identify every documented Solid responsibility and its implementation and test evidence.
- Mark contracts that exist only in prose, only in tests, or only as partial implementation.

### Pass 2: End-to-End Traces

- Trace component creation, access, mutation, removal, and recycling.
- Trace behavior creation, signature changes, membership, and destruction.
- Trace intent creation, authorization, lifetime, selection, cancellation, and cleanup.
- Trace system dispatch, frame failure, recovery policy, and observable logs.
- Trace Lua execution, rollback, limits, diagnostics, and capability invalidation.
- Trace resolved effects, adapter commands, feedback, and observed-state updates.
- Trace simulation, replay, and deterministic serialization boundaries.

### Pass 3: Adversarial Review

- Invalid, stale, recycled, duplicated, and cross-world identifiers.
- Reentrant calls and topology mutation during dispatch.
- Exceptions and partial mutation at every phase boundary.
- Resource exhaustion and all configured limits.
- Asynchronous, delayed, duplicated, and out-of-order adapter results.
- Determinism across repeated runs and supported build configurations.
- Sanitizer-backed lifetime, ownership, and stress coverage.

### Pass 4: Completion Plan

- Classify every finding as implemented, incomplete, missing, intentionally deferred, or out of scope.
- Resolve contradictions between documentation, tests, and implementation.
- Define the smallest ordered milestones that close all accepted Solid gaps.
- Give every milestone explicit success criteria and regression coverage.
- Do not declare Solid complete until all accepted criteria pass.

## Questions the Full Pass Must Resolve

- Where do effect production, command dispatch, feedback ingestion, and state application sit relative to the existing frame phases?
- Is confirmed feedback applied in the same frame or at the beginning of a later frame?
- What identity correlates commands with selected intents without depending on recyclable runtime handles alone?
- Which component data represents desired, commanded, reported, and observed state?
- What retry, timeout, cancellation, and idempotency rules belong in Solid rather than in a specific adapter?
- How are generic typed component values safely encoded across the adapter boundary?
- Which feedback must be persisted for deterministic replay?
- How does the simulator model latency, failure, and device-side normalization without becoming a second runtime?

## Current Decision

Solid is not considered fully complete merely because M1 through M6 pass. Those milestones prove the deterministic decision core and its Lua/CLI exercise path. Completion additionally requires a defined and tested resolved-effect, adapter-command, feedback, and observed-state loop, plus the findings accepted during the future whole-codebase pass.
