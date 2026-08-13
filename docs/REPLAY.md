# Replay Contract

`ReplayProjector` validates records and reconstructs a complete generic `SerializedWorldState` without loading user component types or C++ systems. Projection includes topology, encoded component values, intents, frames, observed state, pending command state, handle generations, and the last applied record.

`ReplayVerifier` is host-assisted. The host registers the original component codecs and system implementations, then the verifier re-executes recorded frame inputs and reports and returns the first record-level divergence with expected and actual evidence.

Lua source is recorded by default so verification is self-contained. Set `LuaExecutionLimits::recordFullSource` to `false` for hash-only evidence; this records a deterministic `fnv1a64:` source identifier and explicitly forfeits self-contained script re-execution. Event-store failure faults `Runtime` before any external effect can be dispatched without durable evidence.
