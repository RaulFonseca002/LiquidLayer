#include "liquid/Runtime.hpp"
#include "liquid/scripting/LuaLifecycleSystem.hpp"
#include "liquid/simulation/InMemoryAdapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

using namespace liquid;
using namespace liquid::scripting;
using namespace liquid::simulation;

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

EffectCodec<Light> light_effect_codec() {
    return {
        AdapterRoute{"scope.light"},
        [](const ComponentName&, const Light& light)
            -> std::optional<ResolvedEffect> {
            return ResolvedEffect{
                AdapterRoute{"scope.light"},
                EffectTarget{"office-device"},
                Value{light.level}};
        },
        [](const Value& observed) {
            return Light{observed.as_signed_integer()};
        }
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

}

TEST_CASE("deferred full loop projects device feedback before lifecycle scripts") {
    RuntimeOptions options;
    options.sessionId = SessionId{400};
    options.feedbackTiming = FeedbackTiming::Deferred;
    options.allowVolatileEffects = true;
    Runtime runtime{options};
    World& world = runtime.world();

    const auto lightType = world.register_component<Light>(
        "example.Light", 1, light_codec());
    world.register_effect_codec(lightType, light_effect_codec());
    const auto scriptType = world.register_component<LuaBehaviorScript>(
        "liquid.LuaBehaviorScript", 1, lua_behavior_script_codec());
    world.add_component(lightType, "office", Light{10});
    world.add_component(scriptType, "lifecycle", LuaBehaviorScript{R"lua(
function on_start(frame)
    solid.watch(access.Light.office)
end

function on_components_changed(frame, changes)
    if changes[1].after.level == 70 then
        solid.cancel(solid.owned_intents.raise_light)
        access.Light.office.propose{
            name = "fallback_light",
            value = {level = 30},
            priority = "high"
        }
    end
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
)lua", 1});

    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(
        lightType, behavior, "office", ComponentAccessMode::ReadWrite);
    world.grant_component_access(
        scriptType, behavior, "lifecycle", ComponentAccessMode::Read);

    auto runner = std::make_shared<LuaBehaviorRunner>();
    runner->expose_component(lightType, "Light", lua_light_codec());
    Signature signature;
    signature.set(scriptType.id);
    world.register_system<LuaLifecycleSystem>(
        signature, SystemPhase::Behavior, scriptType, runner);

    auto adapter = std::make_shared<InMemoryAdapter>(
        AdapterRoute{"scope.light"});
    runtime.register_adapter(adapter);
    runtime.bind_effect_component(
        lightType, "office", EffectTarget{"office-device"});

    FrameInput firstInput;
    firstInput.now = 100;
    const FrameResult first = runtime.run_frame(std::move(firstInput));
    REQUIRE(first.commands.size() == 1);
    REQUIRE(first.commands.front().effect.desiredValue == Value{std::int64_t{70}});
    REQUIRE(world.read_component(lightType, behavior, "office")->level == 10);
    REQUIRE(adapter->state(EffectTarget{"office-device"}) ==
        std::optional<Value>{Value{std::int64_t{70}}});

    FrameInput secondInput;
    secondInput.now = 105;
    const FrameResult second = runtime.run_frame(std::move(secondInput));
    REQUIRE(second.reports.size() == 1);
    REQUIRE(second.reports.front().observedValue ==
        std::optional<Value>{Value{std::int64_t{70}}});
    REQUIRE(second.commands.size() == 1);
    REQUIRE(second.commands.front().effect.desiredValue == Value{std::int64_t{30}});
    REQUIRE(world.read_component(lightType, behavior, "office")->level == 70);

    FrameInput thirdInput;
    thirdInput.now = 110;
    const FrameResult third = runtime.run_frame(std::move(thirdInput));
    REQUIRE(third.reports.size() == 1);
    REQUIRE(third.commands.empty());
    REQUIRE(world.read_component(lightType, behavior, "office")->level == 30);
    REQUIRE(runtime.observed_state(
        AdapterRoute{"scope.light"}, EffectTarget{"office-device"}) ==
        std::optional<Value>{Value{std::int64_t{30}}});
}

TEST_CASE("a disappearing external selection supersedes pending device work") {
    RuntimeOptions options;
    options.sessionId = SessionId{401};
    options.allowVolatileEffects = true;
    Runtime runtime{options};
    World& world = runtime.world();
    const auto lightType = world.register_component<Light>(
        "example.Light", 1, light_codec());
    world.register_effect_codec(lightType, light_effect_codec());
    world.add_component(lightType, "office", Light{10});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(
        lightType, behavior, "office", ComponentAccessMode::ReadWrite);
    const ComponentSlotId slot =
        world.get_components(lightType, behavior).at("office");

    AdapterBehavior adapterBehavior;
    adapterBehavior.silent = true;
    auto adapter = std::make_shared<InMemoryAdapter>(
        AdapterRoute{"scope.light"}, adapterBehavior);
    runtime.register_adapter(adapter);
    runtime.bind_effect_component(
        lightType, "office", EffectTarget{"office-device"});
    const IntentId intent = world.create_intent(
        behavior,
        lightType,
        slot,
        IntentLifetime::persistent(),
        Light{70},
        IntentPriority::High,
        "temporary");

    FrameInput firstInput;
    firstInput.now = 0;
    const FrameResult first = runtime.run_frame(std::move(firstInput));
    REQUIRE(first.commands.size() == 1);
    REQUIRE(runtime.command_status(first.commands.front().commandId) ==
        std::optional<CommandStatus>{CommandStatus::Pending});

    world.destroy_intent(intent);
    FrameInput secondInput;
    secondInput.now = 1;
    const FrameResult second = runtime.run_frame(std::move(secondInput));
    REQUIRE(second.commands.empty());
    REQUIRE(runtime.command_status(first.commands.front().commandId) ==
        std::optional<CommandStatus>{CommandStatus::Superseded});

    FrameInput retryInput;
    retryInput.now = 500;
    REQUIRE(runtime.run_frame(std::move(retryInput)).commands.empty());
    REQUIRE(runtime.command_status(first.commands.front().commandId) ==
        std::optional<CommandStatus>{CommandStatus::Superseded});
}
