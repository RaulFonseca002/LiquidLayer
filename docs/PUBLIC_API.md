# Solid v0.1 Public API

Status: approved contract for version 0.1.0.

Solid is a private, reusable C++20 framework. Installed consumers use `find_package(Liquid 0.1 CONFIG REQUIRED COMPONENTS Core)`; source consumers may use `add_subdirectory`. The exported static targets are `Liquid::Core`, `Liquid::Lua`, and `Liquid::Simulation`.

All public symbols are in `liquid::` or a child namespace. Public headers expose values, world-bound generational handles, `World`, `Runtime`, systems, codecs, effects, adapters, event stores, replay, and simulation APIs. Registries, `Coordinator`, `WorldState`, component storage, raw slots, and implementation helpers are not public API. Template implementation dependencies are installed under `liquid/detail`; consumers must not include those headers directly, and their names and layout may change without compatibility notice.

A public handle contains a world identity, slot, and 32-bit generation. Stale and cross-world handles are rejected. Exhausted generations retire their slots. `SessionId`, `CommandId`, and `RecordId` are monotonic, non-recyclable identifiers within their documented scope.

Component registration requires a stable schema name and version plus a `ComponentCodec<T>`. Optional effect production requires an `EffectCodec<T>` and stable adapter route. Component reads are const borrows invalidated by structural mutation. Mutation uses transactional `update_component` or `replace_component`; validation and encoding succeed before state and journal records commit.

Adapters are registered with `std::shared_ptr<EffectAdapter>` so their lifetime covers every outstanding command and retry. An externally supplied `EventStore*` is a non-owning host service and must outlive the `Runtime`.

The framework makes no stable binary ABI promise before 1.0. Source compatibility follows [COMPATIBILITY.md](COMPATIBILITY.md).
