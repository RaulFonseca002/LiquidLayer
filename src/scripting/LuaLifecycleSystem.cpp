#include "liquid/scripting/LuaLifecycleSystem.hpp"

#include "liquid/Value.hpp"
#include "liquid/world/World.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace liquid::scripting {

ComponentCodec<LuaBehaviorScript> lua_behavior_script_codec() {
    return {
        [](const LuaBehaviorScript& script) {
            Value::Object encoded;
            encoded.emplace("revision", Value{static_cast<std::uint64_t>(script.revision)});
            encoded.emplace("source", Value{script.source});
            return Value{std::move(encoded)};
        },
        [](const Value& value) {
            const Value::Object& encoded = value.as_object();
            const auto source = encoded.find("source");
            const auto revision = encoded.find("revision");
            if (source == encoded.end() || revision == encoded.end() || encoded.size() != 2)
                throw std::invalid_argument("LuaBehaviorScript requires source and revision");
            const std::uint64_t rawRevision = revision->second.as_unsigned_integer();
            if (rawRevision == 0 ||
                rawRevision > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("LuaBehaviorScript revision is out of range");
            return LuaBehaviorScript{
                source->second.as_string(),
                static_cast<std::uint32_t>(rawRevision)};
        }
    };
}

LuaLifecycleSystem::LuaLifecycleSystem(
    ComponentType<LuaBehaviorScript> type,
    std::shared_ptr<LuaBehaviorRunner> behaviorRunner,
    ComponentName componentName
)
    : scriptType(type),
      scriptName(std::move(componentName)),
      runner(std::move(behaviorRunner))
{
    if (scriptType.id == InvalidComponentTypeId)
        throw std::invalid_argument("Lua lifecycle script component type is invalid");
    if (scriptName.empty())
        throw std::invalid_argument("Lua lifecycle script component name cannot be empty");
    if (!runner)
        throw std::invalid_argument("Lua lifecycle runner cannot be null");
}

void LuaLifecycleSystem::on_behavior_removed(BehaviorId behavior) {
    states.erase(behavior);
}

void LuaLifecycleSystem::run(World& world, FrameNumber frame, IntentTime now) {
    for (BehaviorId behavior : behaviors()) {
        const LuaBehaviorScript* script = nullptr;
        try {
            script = world.read_component(scriptType, behavior, scriptName);
        } catch (const std::exception&) {
            states.erase(behavior);
            continue;
        }
        if (!script) {
            states.erase(behavior);
            continue;
        }

        BehaviorState& state = states[behavior];
        if (state.revision != script->revision || state.source != script->source) {
            state = BehaviorState{};
            state.revision = script->revision;
            state.source = script->source;
        }

        std::vector<LuaBehaviorRunner::ComponentChange> changes;
        for (const LuaExecutionResult::Watch& watch : state.watches) {
            std::optional<LuaValue> current = runner->snapshot(world, behavior, watch);
            auto previous = state.snapshots.find({watch.type, watch.name});
            if (current && previous != state.snapshots.end() && previous->second != *current) {
                changes.push_back({watch.type, watch.name, previous->second, *current});
            }
        }

        const IntentTime delta = state.started ? now - state.lastSuccessfulTime : 0;
        LuaExecutionResult result = runner->execute_lifecycle(
            world,
            behavior,
            frame,
            now,
            delta,
            !state.started,
            changes,
            script->source);
        state.lastResult = result;
        if (!result.succeeded())
            continue;

        state.started = true;
        state.lastSuccessfulTime = now;
        for (const LuaExecutionResult::Watch& watch : result.watches) {
            if (std::find(state.watches.begin(), state.watches.end(), watch) ==
                state.watches.end())
                state.watches.push_back(watch);
        }
        state.snapshots.clear();
        for (const LuaExecutionResult::Watch& watch : state.watches) {
            std::optional<LuaValue> snapshot = runner->snapshot(world, behavior, watch);
            if (snapshot)
                state.snapshots.emplace(
                    std::make_pair(watch.type, watch.name), std::move(*snapshot));
        }
    }
}

const LuaExecutionResult* LuaLifecycleSystem::last_result(BehaviorId behavior) const {
    const auto found = states.find(behavior);
    if (found == states.end())
        return nullptr;
    return &found->second.lastResult;
}

}
