# Replay Contract

`ReplayProjector` validates the complete record stream and reconstructs a complete generic `SerializedWorldState` without loading user component types or C++ systems. Projection includes topology, encoded component values, intents, frames, observed state, pending command state and attempt progress, handle generations, and the last applied record. Every checkpoint must match the canonical projection immediately before it; a forged, stale, or internally inconsistent checkpoint is rejected even when it is not the first retained record.

`ReplayVerifier` is host-assisted. The host registers the original component codecs and system implementations, then the verifier re-executes recorded frame inputs and reports and returns the first record-level divergence with expected and actual evidence.

Lua source is recorded by default so verification is self-contained. Set `LuaExecutionLimits::recordFullSource` to `false` for hash-only evidence; this records a deterministic `fnv1a64:` source identifier and explicitly forfeits self-contained script re-execution. Event-store failure faults `Runtime` before any external effect can be dispatched without durable evidence.

## Operations guide

*(formerly `docs/REPLAY_GUIDE.md`, "Replay Operations Guide"; text verbatim, headings demoted one level. The contract above and [EVENT_FORMAT_V1.md](EVENT_FORMAT_V1.md) remain normative where this guide restates them.)*

The normative replay contract is [REPLAY.md](#replay-contract); the binary contract is [EVENT_FORMAT_V1.md](EVENT_FORMAT_V1.md).

### Choose the replay mode

- Use `ReplayProjector` to reconstruct a generic `SerializedWorldState` from encoded records without application component types or C++ systems.
- Use `ReplayVerifier` when the host can register the original component codecs and system implementations. Verification re-executes recorded frame inputs/reports and stops at the first record-level divergence.

Projection is self-contained. Execution verification of compiled C++ systems necessarily depends on the original host code.

### Preserve verification inputs

Record full Lua source by default. Hash-only recording reduces stored script content but forfeits self-contained Lua re-execution. Preserve the exact engine version, component schema names/versions, system names/versions, feedback timing, explicit frame inputs, and reports associated with a session.

### Operate a durable session

1. Open and validate the session file before starting Runtime.
2. Reject mid-file corruption, unsupported versions, sequence gaps, and bounds violations.
3. Allow writable recovery to truncate only a corrupt or incomplete final batch, then require a durable recovery record.
4. Request checkpoints explicitly; verify their complete shape, session,
   replay anchor, pending commands, and attempt progress against the canonical
   pre-checkpoint projection before using one as a retention boundary.
5. Compact by creating, validating, flushing, and atomically replacing a new generation. Never delete arbitrary records in place.

If event-store append or flush fails, Runtime must fault before dispatching an external effect without durable evidence. Buffered durability is limited to disposable simulations and must not be presented as a durable replay source.

Both stores project and validate the candidate retained stream before replacing
current records. Failure before replacement preserves the prior generation.
For files, directory-flush failure after installation leaves the replacement
installed and faults the store; do not claim rollback past that boundary. Runtime
restore validates the whole stream before rebuilding live effect state, so a
retained retry resumes at the recorded next attempt rather than restarting its
schedule. V1 checkpoints embed historical arrays, so their Value limits still
bound session history; physical retention does not provide indefinite compaction.

### Diagnose divergence

Start with the first divergent `RecordId`, not the final projected state. Compare recorded and re-executed frame input, system identity/version, selected encoded desire, command decision, report ordering, and observed-state transition. Later mismatches are usually consequences of the first divergence.
