# Solid v0.1 Public API

Status: approved contract for version 0.1.0.

Solid is a private, reusable C++20 framework. Installed consumers use `find_package(Liquid 0.1 CONFIG REQUIRED COMPONENTS Core)`; source consumers may use `add_subdirectory`. The exported static targets are `Liquid::Core`, `Liquid::Lua`, and `Liquid::Simulation`.

All public symbols are in `liquid::` or a child namespace. Public headers expose values, world-bound generational handles, `World`, `Runtime`, systems, codecs, effects, adapters, event stores, replay, and simulation APIs. Registries, `Coordinator`, `WorldState`, component storage, raw slots, and implementation helpers are not public API. Template implementation dependencies are installed under `liquid/detail`; consumers must not include those headers directly, and their names and layout may change without compatibility notice.

A public handle contains a world identity, slot, and 32-bit generation. Stale and cross-world handles are rejected. Exhausted generations retire their slots. `SessionId`, `CommandId`, and `RecordId` are monotonic, non-recyclable identifiers within their documented scope.

Component registration requires a stable schema name and version plus a `ComponentCodec<T>`. Optional effect production requires a bidirectional `EffectCodec<T>` and stable adapter route: selected encoded values become commands, while authoritative device values decode back into the component. Component reads are const borrows invalidated by structural mutation. Mutation uses transactional `update_component` or `replace_component`; validation and encoding succeed before state and journal records commit.

Systems register in the `Input`, `Behavior`, or `Decision` phase. A frame applies queued feedback before running Input systems, then runs Behavior and Decision systems, resolves every live intent target, derives effects, dispatches, and records completion. `ComponentControl` declares whether a selected intent is evidence only, commits internal state, or drives a bound external effect. External components change only after authoritative reports or revisioned observations.

`Liquid::Lua` provides `LuaBehaviorScript`, `LuaLifecycleSystem`, and `LuaBehaviorRunner`. Lifecycle scripts execute in one fresh sandbox per behavior per frame and may define `on_start(frame)`, `on_components_changed(frame, changes)`, and `on_frame(frame)`. Proposals, cancellations, and watch registrations commit as one successful callback bundle; a callback failure commits none of them. Script state lives in ordinary codec-backed components, never in a retained Lua VM.

Adapters are registered with `std::shared_ptr<EffectAdapter>` so their lifetime covers every outstanding command and retry. An externally supplied `EventStore*` is a non-owning host service and must outlive the `Runtime`.

The framework makes no stable binary ABI promise before 1.0. Source compatibility follows [SUPPORT.md § Compatibility](SUPPORT.md#compatibility).

## Threading

*(formerly `docs/THREADING.md`, "Threading and Ownership Contract"; text verbatim)*

`World`, `Runtime`, systems, adapters during synchronous dispatch, and writable event stores are confined to the creating thread. Public entry points reject use from another thread before mutation. Hosts provide any broader synchronization.

`FeedbackSender` is the deliberate cross-thread boundary. It is thread-safe, bounded, and accepts immutable `EffectReport` values from concurrent producers. Queue overflow is reported to the producer and never silently drops a report. Shutdown prevents new sends, wakes waiters, and permits deterministic draining or discard according to the selected close operation. Destroying `Runtime` invalidates its senders safely.

Runtime snapshots the queued reports at the beginning of a frame. Reports arriving after that snapshot wait for the next frame. Immediate feedback is limited to a report returned synchronously by the dispatch call; asynchronous reports always use the queue.
