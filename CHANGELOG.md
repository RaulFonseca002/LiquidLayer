# Changelog

All notable framework changes will be recorded here. Dates are release dates, not implementation dates.

## Unreleased

- 🐛 Allow recreated components to immediately reuse their external effect target without advancing a frame.
- ✨ Liquid L0 (awaiting review): immutable `LuaValueSchema` and `validate_lua_value`; `LuaModelBindingMetadata` with `symmetric_metadata`; a four-argument `LuaBehaviorRunner::expose_component` that enforces write schemas on proposals and read schemas on snapshots; and `LuaBehaviorRunner::capability_manifest`, which returns a copied, ordered capability manifest with Lua access expressions and freezes registration on the first success. The three-argument overload is unchanged. Tests: `lua_schema`, `lua_manifest`.
- ✨ Liquid L1 (owner-approved): opt-in `Liquid::Authoring` component (`LIQUID_BUILD_AUTHORING`, default `OFF`; `COMPONENTS Authoring`) with `AuthoringSession` for host-selected prospective scopes and immutable, unexecuted `BehaviorProposal` records; revisioned scopes with revoke, re-resolution of targets on every discover/submit, caller checks and bounded capacity. `Liquid::Lua` gains `LuaScopeGrant`, the host-only `LuaBehaviorRunner::scope_manifest`, `LuaManifestCaptureKind` and the additive `LuaManifestErrorCode::InvalidGrant`. Default builds and existing consumers are unchanged. Tests: `authoring_scope`, `authoring_proposal`, `authoring_package`.
- 🐛 Liquid L1 correction (owner-approved): `AuthoringSession` enforces the contract's session-wide logical payload ceiling, `AuthoringLimits::maxSessionPayloadBytes` (default 64 MiB, may only be lowered), over retained scopes and proposals; over-ceiling `create_scope`, `replace_scope` and `submit` fail with `LimitExceeded` and change nothing, and `revoke_scope` releases capacity. Test: `authoring_session_ceiling`.
- ✨ Liquid L2 (owner-approved): isolated deterministic proposal evaluation in `Liquid::Authoring`. `AuthoringSession` gains the trusted-host `register_evaluation_suite`, the caller-checked `evaluate`, which runs a proposal's exact source against every case of a host-registered `EvaluationSuite` in fresh isolated Runtimes and returns an immutable `EvaluationRecord` without touching the live World or session clock, and the trusted-host `normalized_trace`. Frame summaries count only the candidate's granted targets, and the trusted-host-only `world_totals` returns whole-world counts derived from the kept trace (owner decision on L2B-14). `AuthoringLimits` gains bounded evaluation cases, frames, event-store records and bytes, and response bytes, and evaluations are charged to `maxSessionPayloadBytes`. `Liquid::Lua` gains `LuaScriptSelection::SingleReadable` and an additive `LuaLifecycleSystem` constructor that runs each behavior's one readable script; the named constructor is unchanged. Tests: `authoring_evaluation`, extended `lua_lifecycle`, `authoring_package`.

## 0.1.0 — 2026-08-22

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
- Solid Scope full-loop simulation console: a `liquid.trace.v2` NDJSON
  stream over the loopback bridge with distinct desire, command, device,
  feedback, and confirmed-state lanes, a seven-phase runtime rail, and a
  headless renderer self-test harness.
- Maintainability decomposition of the runtime and file event store into
  cohesive translation units under `src/runtime/` and `src/events/`, with
  no public API or behavior change.
- Atomic lifecycle-script cancellation/proposal bundles, including rollback
  under capacity, codec, and topology-reentry failures.
- Strict whole-stream checkpoint, retention, and restore validation, including
  retained command-attempt progress and retry-safe store replacement.
- Retry-safe adapter registration: routes become visible only after their
  complete registration evidence is durably appended.
- Hardened Scope loopback request validation and Unix process-group cleanup,
  plus release-build assertion guards and a package export limited to the
  documented `Core`, `Lua`, and `Simulation` targets.
