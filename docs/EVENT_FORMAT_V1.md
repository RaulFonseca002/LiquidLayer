# Solid Event File Format v1

`FileEventStore` is a single-writer custom binary log for one session. It detects accidental corruption; it is neither encrypted nor tamper-evident.

The header stores magic bytes, file-format version, `SessionId`, engine version, feedback timing, file generation, and CRC32C. It is followed by length-prefixed versioned record batches. Every logical record has a permanent strictly increasing sequence number. All strings, bytes, collections, nesting, batches, and records are bounded. `Value` encoding is canonical: explicit type tags, fixed integer encodings, finite IEEE-754 doubles, UTF-8 strings, byte strings, arrays in order, and objects in key-sorted order.

Record families cover session/configuration, topology, component mutation, intent lifecycle, frame input/result, resolution, command issuance and attempts, reports, observed-state transitions, failures, checkpoints, recovery, and retention.

Opening a file scans and validates every batch. A corrupt or truncated final batch may be truncated to the last valid offset only in writable recovery mode, followed by a durable recovery record. Mid-file corruption, sequence gaps, unsupported versions, or bounds violations are hard errors. Command issuance and attempts are flushed before dispatch. Completed and failed frames are flushed before frame completion is reported. Buffered mode is explicitly weaker and limited to disposable simulations.

Checkpoints are host-requested complete generic projections including pending commands, command-attempt progress, handle generations, and replay position. Each checkpoint is validated against the canonical state projected immediately before it; non-leading checkpoints do not bypass this check. Retention may remove only data before a verified checkpoint by writing and validating a replacement generation, flushing it, and atomically replacing the old file. The commit boundary is the atomic file replacement: any failure before the replacement is installed preserves the original bytes and generation, and the replacement file is discarded. Once the replacement is installed, a failure while flushing the parent directory leaves the replacement installed with its new generation and faults the store, because its durability is then uncertain; reopening the file recovers the installed replacement. Arbitrary in-place deletion is forbidden.

**Known limitation (finite sessions):** a checkpoint embeds the complete historical record arrays (frames, resolutions, command attempts, reports, observations, failures, recoveries, retentions, scripts) that replay equivalence currently requires. Retention removes physical records before the checkpoint, but those records remain inside the checkpoint payload, so repeated checkpoint/retention cycles do not bound the payload. When the embedded history exceeds the global `Value` limits (4,096 array items or 16,384 total nodes), constructing the next checkpoint fails with a bounded error and the session can no longer be compacted. Retention therefore bounds file size only until that threshold; it does not promise indefinitely bounded session history. Bounding current state separately from historical evidence is a future checkpoint design decision with its own payload/version strategy; it is not part of Event Format v1. The regression `checkpoint payloads embed retained history until the value node limit` in `tests/test_replay.cpp` makes the current behavior explicit.

---

## Using FileEventStore

*(Explanatory, non-normative. Everything above this line is the frozen v1 contract. Formerly `docs/FILE_FORMAT_GUIDE.md`, "File Event Store Guide"; text verbatim, headings demoted one level.)*

[EVENT_FORMAT_V1.md](#solid-event-file-format-v1) is the normative v1 contract. This guide summarizes safe operational use; it does not replace the binary layout or golden fixtures.

### Storage assumptions

Use one local-filesystem file and one writer for each session. Keep it on a filesystem whose locking, flush, and atomic-replace behavior meets the host platform contract. Network filesystems, multiple writers, encryption, and tamper evidence are outside v0.1 support.

Protect files with operating-system access controls: logs may contain component values and full Lua source. CRC32C detects accidental corruption only.

### Durability modes

Durable mode synchronizes command issuance and attempt evidence before adapter dispatch, and synchronizes each completed or failed frame batch before reporting frame completion. Use this mode for any run that can produce an external effect.

Buffered mode is explicitly weaker and is appropriate only for disposable simulation. A buffered log may lose its latest batches after a crash.

### Open and recovery

Readers validate the header, CRCs, versions, bounds, and permanent logical sequence numbers while scanning. A sequence gap or corruption before the final batch is a hard error. Writable recovery may remove only an incomplete or corrupt final batch, return to the last valid byte, and append a durable recovery record.

Do not copy a live file as a checkpoint and do not repair bytes manually. A checkpoint is a host-requested record containing the complete generic projection, pending command state and attempt progress, handle generations, and replay position. Every checkpoint, including one retained after earlier history, must match the canonical projection immediately before it.

### Retention

Retention starts only at a verified checkpoint. Write and validate the replacement
generation, flush it, atomically replace the prior file, then flush required
parent-directory metadata. Failures before installation preserve the original;
a directory-flush failure after installation leaves the replacement installed
and faults the store with uncertain durability. The memory store follows its
project-before-replace rule without filesystem durability operations.

V1 checkpoint payloads retain historical arrays and eventually reach Value
limits. Repeated retention is not an indefinite history bound; see
[EVENT_FORMAT_V1.md](#solid-event-file-format-v1) before designing a long-running session.

Unknown file, batch, record, or canonical-value versions are rejected unless the format contract explicitly defines a skippable envelope. Use the golden binary fixtures and byte-level truncation/corruption tests before changing any encoder or decoder.
