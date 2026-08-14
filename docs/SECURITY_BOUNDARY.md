# Security Boundary

Solid is an in-process framework, not a security sandbox for hostile native host code. Hosts control files, threads, adapters, codecs, systems, and operating-system access. Event files can contain scripts and component data and must be protected with OS permissions.

The Lua boundary is capability-based and bounded: scripts receive copied snapshots and allowlisted proposal functions, never runtime internals or owner selection. Codecs are trusted validation boundaries. Decoders enforce size, depth, UTF-8, finite-number, version, and sequence limits before allocation or mutation.

The file store detects accidental corruption with CRC32C but provides no confidentiality, authentication, or tamper evidence. v0.1 supports one local-filesystem writer with an OS lock; network filesystems are unsupported. Adapters are trusted integration code but do not receive state authority. Reports are untrusted data until correlated and validated by `Runtime`.
