#pragma once

#include "liquid/ComponentCodec.hpp"
#include "liquid/System.hpp"
#include "liquid/scripting/LuaBehaviorRunner.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace liquid::scripting {

struct LuaBehaviorScript {
    std::string source;
    std::uint32_t revision = 1;

    friend bool operator==(const LuaBehaviorScript&, const LuaBehaviorScript&) = default;
};

ComponentCodec<LuaBehaviorScript> lua_behavior_script_codec();

enum class LuaScriptSelection {
    SingleReadable
};

class LuaLifecycleSystem final : public System {
private:
    struct BehaviorState {
        ComponentName scriptSlotName;
        ComponentSlotId scriptSlot{};
        std::uint32_t revision = 0;
        std::string source;
        bool started = false;
        IntentTime lastSuccessfulTime = 0;
        std::vector<LuaExecutionResult::Watch> watches;
        std::map<std::pair<TypeName, ComponentName>, LuaValue> snapshots;
        LuaExecutionResult lastResult;
    };

    ComponentType<LuaBehaviorScript> scriptType;
    // A name selects that fixed slot; nullopt selects the single readable slot.
    std::optional<ComponentName> scriptName;
    std::shared_ptr<LuaBehaviorRunner> runner;
    std::map<BehaviorId, BehaviorState> states;

public:
    static constexpr std::string_view stableName =
        "liquid.scripting.LuaLifecycleSystem";
    static constexpr std::uint32_t version = 1;

    LuaLifecycleSystem(
        ComponentType<LuaBehaviorScript> type,
        std::shared_ptr<LuaBehaviorRunner> behaviorRunner,
        ComponentName componentName = "lifecycle"
    );

    LuaLifecycleSystem(
        ComponentType<LuaBehaviorScript> type,
        std::shared_ptr<LuaBehaviorRunner> behaviorRunner,
        LuaScriptSelection selection
    );

    void on_behavior_removed(BehaviorId behavior) override;
    void run(World& world, FrameNumber frame, IntentTime now) override;

    const LuaExecutionResult* last_result(BehaviorId behavior) const;
};

}
