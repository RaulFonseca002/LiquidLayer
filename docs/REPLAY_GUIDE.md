# Replay Operations Guide

The normative replay contract is [REPLAY.md](REPLAY.md); the binary contract is [EVENT_FORMAT_V1.md](EVENT_FORMAT_V1.md).

## Choose the replay mode

- Use `ReplayProjector` to reconstruct a generic `SerializedWorldState` from encoded records without application component types or C++ systems.
- Use `ReplayVerifier` when the host can register the original component codecs and system implementations. Verification re-executes recorded frame inputs/reports and stops at the first record-level divergence.

Projection is self-contained. Execution verification of compiled C++ systems necessarily depends on the original host code.

## Preserve verification inputs

Record full Lua source by default. Hash-only recording reduces stored script content but forfeits self-contained Lua re-execution. Preserve the exact engine version, component schema names/versions, system names/versions, feedback timing, explicit frame inputs, and reports associated with a session.

## Operate a durable session

1. Open and validate the session file before starting Runtime.
2. Reject mid-file corruption, unsupported versions, sequence gaps, and bounds violations.
3. Allow writable recovery to truncate only a corrupt or incomplete final batch, then require a durable recovery record.
4. Request checkpoints explicitly; verify their complete shape, session,
   replay anchor, pending commands, and attempt progress against the canonical
   pre-checkpoint projection before using one as a retention boundary.
5. Compact by creating, validating, flushing, and atomically replacing a new generation. Never delete arbitrary records in place.

If event-store append or flush fails, Runtime must fault before dispatching an external effect without durable evidence. Buffered durability is limited to disposable simulations and must not be presented as a durable replay source.

Retention is transactional: both memory and file stores project and validate
the candidate retained stream before replacing their current records. A
failure leaves the prior records and file generation unchanged. Runtime
restore validates the whole stream before rebuilding live effect state, so a
retained retry resumes at the recorded next attempt rather than restarting its
schedule.

## Diagnose divergence

Start with the first divergent `RecordId`, not the final projected state. Compare recorded and re-executed frame input, system identity/version, selected encoded desire, command decision, report ordering, and observed-state transition. Later mismatches are usually consequences of the first divergence.
