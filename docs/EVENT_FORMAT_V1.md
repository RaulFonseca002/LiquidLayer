# Solid Event File Format v1

`FileEventStore` is a single-writer custom binary log for one session. It detects accidental corruption; it is neither encrypted nor tamper-evident.

The header stores magic bytes, file-format version, `SessionId`, engine version, feedback timing, file generation, and CRC32C. It is followed by length-prefixed versioned record batches. Every logical record has a permanent strictly increasing sequence number. All strings, bytes, collections, nesting, batches, and records are bounded. `Value` encoding is canonical: explicit type tags, fixed integer encodings, finite IEEE-754 doubles, UTF-8 strings, byte strings, arrays in order, and objects in key-sorted order.

Record families cover session/configuration, topology, component mutation, intent lifecycle, frame input/result, resolution, command issuance and attempts, reports, observed-state transitions, failures, checkpoints, recovery, and retention.

Opening a file scans and validates every batch. A corrupt or truncated final batch may be truncated to the last valid offset only in writable recovery mode, followed by a durable recovery record. Mid-file corruption, sequence gaps, unsupported versions, or bounds violations are hard errors. Command issuance and attempts are flushed before dispatch. Completed and failed frames are flushed before frame completion is reported. Buffered mode is explicitly weaker and limited to disposable simulations.

Checkpoints are host-requested complete generic projections including pending commands, command-attempt progress, handle generations, and replay position. Each checkpoint is validated against the canonical state projected immediately before it; non-leading checkpoints do not bypass this check. Retention may remove only data before a verified checkpoint by writing and validating a replacement generation, flushing it, and atomically replacing the old file. Any failure preserves the prior bytes and generation. Arbitrary in-place deletion is forbidden.
