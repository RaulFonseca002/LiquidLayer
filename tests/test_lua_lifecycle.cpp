#include "liquid/Runtime.hpp"
#include "liquid/scripting/LuaLifecycleSystem.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>

using namespace liquid;
using namespace liquid::scripting;

namespace {

struct Light {
    std::int64_t level = 0;
};

ComponentCodec<Light> light_codec() {
    return {
        [](const Light& light) { return Value{light.level}; },
        [](const Value& value) { return Light{value.as_signed_integer()}; }
    };
}

LuaComponentCodec<Light> lua_light_codec() {
    return {
        [](const Light& light) {
            return LuaValue{LuaValue::Table{{"level", LuaValue{light.level}}}};
        },
        [](const LuaValue& value) {
            return Light{value.as_table().at("level").as_integer()};
        }
    };
}

Signature script_signature(ComponentType<LuaBehaviorScript> scriptType) {
    Signature signature;
    signature.set(scriptType.id);
    return signature;
}

}

TEST_CASE("Lua lifecycle callbacks drive the same-frame Solid loop") {
    RuntimeOptions options;
    options.sessionId = SessionId{1};
    options.allowVolatileEffects = true;
    Runtime runtime(options);
    World& world = runtime.world();
    const auto lightType = world.register_component<Light>(
        "example.Light", 1, light_codec());
    const auto scriptType = world.register_component<LuaBehaviorScript>(
        "liquid.LuaBehaviorScript", 1, lua_behavior_script_codec());

    const std::string source = R"lua(
function on_start(frame)
    assert(frame.number == 0)
    solid.watch(access.Light.office)
end

function on_components_changed(frame, changes)
    assert(frame.number == 1)
    assert(#changes == 1)
    assert(changes[1].before.level == 10)
    assert(changes[1].after.level == 70)
    solid.cancel(solid.owned_intents.raise_light)
    access.Light.office.propose{
        name = "lower_light",
        value = {level = 30},
        priority = "high"
    }
end

function on_frame(frame)
    if frame.number == 0 then
        access.Light.office.propose{
            name = "raise_light",
            value = {level = 70},
            priority = "high"
        }
    end
end
)lua";

    world.add_component(lightType, "office", Light{10});
    world.add_component(
        scriptType, "lifecycle", LuaBehaviorScript{source, 1});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(
        lightType, behavior, "office", ComponentAccessMode::ReadWrite);
    world.grant_component_access(
        scriptType, behavior, "lifecycle", ComponentAccessMode::Read);

    auto runner = std::make_shared<LuaBehaviorRunner>();
    runner->expose_component(lightType, "Light", lua_light_codec());
    world.register_system<LuaLifecycleSystem>(
        script_signature(scriptType),
        SystemPhase::Behavior,
        scriptType,
        runner);
    runtime.configure_component(lightType, "office", ComponentControl::InternalState);

    FrameInput firstInput;
    firstInput.now = 100;
    const FrameLog first = runtime.run_frame(std::move(firstInput)).frame;
    REQUIRE(first.completed);
    const auto& firstSystem = world.get_system<LuaLifecycleSystem>();
    REQUIRE(firstSystem.last_result(behavior) != nullptr);
    INFO(firstSystem.last_result(behavior)->diagnostic);
    REQUIRE(firstSystem.last_result(behavior)->succeeded());
    REQUIRE(world.read_component(lightType, behavior, "office")->level == 70);
    const std::optional<IntentId> raised = world.intent_named(behavior, "raise_light");
    REQUIRE(raised.has_value());

    FrameInput secondInput;
    secondInput.now = 105;
    const FrameLog second = runtime.run_frame(std::move(secondInput)).frame;
    REQUIRE(second.completed);
    REQUIRE(world.read_component(lightType, behavior, "office")->level == 30);
    REQUIRE(!world.intent_exists(*raised));
    REQUIRE(!world.intent_named(behavior, "raise_light").has_value());
    REQUIRE(world.intent_named(behavior, "lower_light").has_value());

    const auto& system = world.get_system<LuaLifecycleSystem>();
    REQUIRE(system.last_result(behavior) != nullptr);
    REQUIRE(system.last_result(behavior)->succeeded());
    REQUIRE(system.last_result(behavior)->cancelledIntents ==
        std::vector<IntentId>{*raised});
}

TEST_CASE("failed lifecycle bundles commit no proposals or watches") {
    RuntimeOptions options;
    options.sessionId = SessionId{2};
    options.allowVolatileEffects = true;
    Runtime runtime(options);
    World& world = runtime.world();
    const auto lightType = world.register_component<Light>(
        "example.Light", 1, light_codec());
    const auto scriptType = world.register_component<LuaBehaviorScript>(
        "liquid.LuaBehaviorScript", 1, lua_behavior_script_codec());

    world.add_component(lightType, "office", Light{10});
    world.add_component(scriptType, "lifecycle", LuaBehaviorScript{R"lua(
function on_start(frame)
    solid.watch(access.Light.office)
end
function on_frame(frame)
    access.Light.office.propose{name = "never_committed", value = {level = 70}}
    error("callback failed")
end
)lua", 1});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(
        lightType, behavior, "office", ComponentAccessMode::ReadWrite);
    world.grant_component_access(
        scriptType, behavior, "lifecycle", ComponentAccessMode::Read);

    auto runner = std::make_shared<LuaBehaviorRunner>();
    runner->expose_component(lightType, "Light", lua_light_codec());
    world.register_system<LuaLifecycleSystem>(
        script_signature(scriptType),
        SystemPhase::Behavior,
        scriptType,
        runner);
    runtime.configure_component(lightType, "office", ComponentControl::InternalState);

    FrameInput input;
    input.now = 100;
    const FrameLog frame = runtime.run_frame(std::move(input)).frame;
    REQUIRE(frame.completed);
    REQUIRE(world.intent_count(behavior) == 0);
    REQUIRE(world.read_component(lightType, behavior, "office")->level == 10);

    const auto& system = world.get_system<LuaLifecycleSystem>();
    REQUIRE(system.last_result(behavior) != nullptr);
    REQUIRE(!system.last_result(behavior)->succeeded());
    REQUIRE(system.last_result(behavior)->watches.empty());
}
