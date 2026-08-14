#pragma once

#include "liquid/ComponentCodec.hpp"
#include "liquid/System.hpp"
#include "liquid/scripting/LuaBehaviorRunner.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace liquid::scripting {

struct LuaBehaviorScript {
    std::string source;
    std::uint32_t revision = 1;

    friend bool operator==(const LuaBehaviorScript&, const LuaBehaviorScript&) = default;
};

ComponentCodec<LuaBehaviorScript> lua_behavior_script_codec();

class LuaLifecycleSystem final : public System {
private:
    struct BehaviorState {
        std::uint32_t revision = 0;
        std::string source;
        bool started = false;
        IntentTime lastSuccessfulTime = 0;
        std::vector<LuaExecutionResult::Watch> watches;
        std::map<std::pair<TypeName, ComponentName>, LuaValue> snapshots;
        LuaExecutionResult lastResult;
    };

    ComponentType<LuaBehaviorScript> scriptType;
    ComponentName scriptName;
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

    void on_behavior_removed(BehaviorId behavior) override;
    void run(World& world, FrameNumber frame, IntentTime now) override;

    const LuaExecutionResult* last_result(BehaviorId behavior) const;
};

}
