# Solid Lifecycle Scripting

Solid runs each frame in a fixed order:

```text
queued feedback and external observations
→ Input systems
→ Behavior systems
→ Decision systems
→ intent resolution
→ internal commits and external command dispatch
```

Register hardware-ingest or simulator-input systems in `SystemPhase::Input`, `LuaLifecycleSystem` in `SystemPhase::Behavior`, and policy or post-script decision systems in `SystemPhase::Decision`. External component values are projected before Input systems, so every later phase reads the current authoritative observation. Selected external desires do not mutate the component optimistically; the component changes only after an applied report or external observation.

## Script component and callbacks

Register `LuaBehaviorScript` with `lua_behavior_script_codec()`, attach a component named `lifecycle` to a behavior, and register `LuaLifecycleSystem` with a shared `LuaBehaviorRunner`. Expose only the component codecs that script authors may read or propose.

A lifecycle source may define any of these optional callbacks:

```lua
function on_start(frame)
    solid.watch(access.Light.office)
end

function on_components_changed(frame, changes)
    -- changes[n] has type, name, before, and after snapshots
end

function on_frame(frame)
    -- frame has number, now_ms, and delta_ms
end
```

On the first successful execution of a script revision, callbacks run in `on_start`, `on_components_changed`, `on_frame` order. The initial snapshot does not generate a change callback. Watches requested by `on_start` activate only after the complete callback bundle succeeds. A failed bundle is retried on a later frame and does not commit proposals, cancellations, or watches.

Each behavior receives a fresh Lua VM every frame. Persistent script state must therefore be an ordinary serializable component exposed through `access`, where it participates in codecs, mutation evidence, checkpoints, and replay.

## Named intent transaction

Lifecycle proposals require an owner-scoped stable name:

```lua
access.Light.office.propose{
    name = "evening_light",
    value = {level = 70},
    priority = "high"
}
```

`solid.owned_intents` contains immutable snapshots of the executing behavior's live named intents. Scripts may pass one of those opaque snapshots to `solid.cancel`; they cannot select another behavior's intent or supply a raw ID.

```lua
local prior = solid.owned_intents.evening_light
if prior then
    solid.cancel(prior)
end
```

The runtime validates the complete callback bundle before applying its cancellations and proposals. Commit is one transaction: cancellation, replacement, and every new typed intent either all become visible or the exact prior intent records, indexes, sequences, and pool capacity are restored. Codec errors, capacity exhaustion, and forbidden structural topology changes during commit therefore cannot leave a partial bundle. Duplicate live names and duplicate names inside one bundle are rejected deterministically.
