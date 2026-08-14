# File Event Store Guide

[EVENT_FORMAT_V1.md](EVENT_FORMAT_V1.md) is the normative v1 contract. This guide summarizes safe operational use; it does not replace the binary layout or golden fixtures.

## Storage assumptions

Use one local-filesystem file and one writer for each session. Keep it on a filesystem whose locking, flush, and atomic-replace behavior meets the host platform contract. Network filesystems, multiple writers, encryption, and tamper evidence are outside v0.1 support.

Protect files with operating-system access controls: logs may contain component values and full Lua source. CRC32C detects accidental corruption only.

## Durability modes

Durable mode synchronizes command issuance and attempt evidence before adapter dispatch, and synchronizes each completed or failed frame batch before reporting frame completion. Use this mode for any run that can produce an external effect.

Buffered mode is explicitly weaker and is appropriate only for disposable simulation. A buffered log may lose its latest batches after a crash.

## Open and recovery

Readers validate the header, CRCs, versions, bounds, and permanent logical sequence numbers while scanning. A sequence gap or corruption before the final batch is a hard error. Writable recovery may remove only an incomplete or corrupt final batch, return to the last valid byte, and append a durable recovery record.

Do not copy a live file as a checkpoint and do not repair bytes manually. A checkpoint is a host-requested record containing the complete generic projection, pending command state, handle generations, and replay position.

## Retention

Retention starts only at a verified checkpoint. Write the checkpoint and later batches to a replacement generation, validate the whole replacement, flush file and required directory metadata, then atomically replace the prior generation and record the pruned sequence range. Keep the original file when any stage fails.

Unknown file, batch, record, or canonical-value versions are rejected unless the format contract explicitly defines a skippable envelope. Use the golden binary fixtures and byte-level truncation/corruption tests before changing any encoder or decoder.
