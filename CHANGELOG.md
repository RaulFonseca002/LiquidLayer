# Changelog

All notable framework changes will be recorded here. Dates are release dates, not implementation dates.

## Unreleased

### Planned for 0.1.0

- Private C++20 static framework components `Liquid::Core`, `Liquid::Lua`, and `Liquid::Simulation`.
- World-bound generational handles, bounded canonical values, codec-backed components, and immutable encoded intents.
- Durable memory/file event stores, projection replay, and host-assisted verification.
- Complete topology, component, intent, frame, command, report, observed-state,
  and Lua execution evidence; Lua source is retained by default with an
  explicit hash-only option.
- Adapter command lifecycle with deferred or immediate feedback, supersession, deterministic retries, and indeterminate recovery.
- Ordered Input/Behavior/Decision systems, revisioned external observations,
  bidirectional effect projection, and transactional Lua lifecycle callbacks
  with watches and owner-scoped named-intent cancellation.
- Bounded feedback, dispatcher caches, simulator queues, command history, and
  world evidence journals, with deterministic 5,000-target/1,000-frame stress.
- Source-tree and installed-package consumers across the supported compiler platforms.
- Official Catch2 v3.8.1 amalgamated tests under CTest, vendored for offline
  reproducibility.
- Solid Scope full-loop simulation console on the experiment branch: a
  `liquid.trace.v2` NDJSON stream over the loopback bridge with distinct
  desire, command, device, feedback, and confirmed-state lanes, a
  seven-phase runtime rail, and a headless renderer self-test harness.

The 0.1.0 entry will be dated only after the traceability matrix and release gates are complete.
