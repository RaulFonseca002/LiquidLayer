#include <liquid/Runtime.hpp>
#include <liquid/scripting/LuaBehaviorRunner.hpp>
#include <liquid/scripting/LuaCapabilityManifest.hpp>

#include <cstdint>

namespace {

struct Light {
    std::int64_t level = 0;
};

}

int main() {
    namespace lua = liquid::scripting;

    liquid::Runtime runtime;
    liquid::World& world = runtime.world();
    const auto lightType = world.register_component<Light>(
        "example.Light",
        1,
        liquid::ComponentCodec<Light>{
            [](const Light& light) { return liquid::Value{light.level}; },
            [](const liquid::Value& value) { return Light{value.as_signed_integer()}; }
        }
    );
    world.add_component(lightType, "office", Light{40});
    const liquid::BehaviorId behavior = world.create_behavior();
    world.grant_component_access(lightType, behavior, "office", liquid::ComponentAccessMode::ReadWrite);

    lua::LuaBehaviorRunner runner;
    runner.expose_component(
        lightType,
        "Light",
        lua::LuaComponentCodec<Light>{
            [](const Light& light) { return lua::LuaValue{lua::LuaValue::Table{{"level", lua::LuaValue{light.level}}}}; },
            [](const lua::LuaValue& value) { return Light{value.as_table().at("level").as_integer()}; }
        },
        lua::symmetric_metadata(
            lua::LuaValueSchema::object({{"level", lua::LuaValueSchema::integer(0, 100), true}}),
            "Office light level")
    );

    const lua::LuaManifestResult manifest = runner.capability_manifest(world, behavior, 0);
    if (!manifest.ok() || manifest.manifest().capabilities().size() != 1)
        return 1;

    const lua::LuaExecutionResult result = runner.execute(
        world,
        behavior,
        0,
        "access.Light.office.propose{value = {level = access.Light.office.value.level + 2}}"
    );

    return result.succeeded() ? 0 : 1;
}
