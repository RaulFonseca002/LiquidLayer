#include "liquid/ComponentCodec.hpp"
#include "liquid/world/World.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace liquid;

namespace {

struct AliasedComponent {
    std::shared_ptr<std::vector<std::int64_t>> values;
};

liquid::ComponentCodec<AliasedComponent> aliased_codec() {
    return {
        [](const AliasedComponent& component) {
            if (!component.values)
                throw std::invalid_argument("AliasedComponent values are required");

            liquid::Value::Array values;

            for (std::int64_t value : *component.values)
                values.emplace_back(value);

            return liquid::Value(std::move(values));
        },
        [](const liquid::Value& encoded) {
            auto values = std::make_shared<std::vector<std::int64_t>>();

            for (const liquid::Value& value : encoded.as_array())
                values->push_back(value.as_signed_integer());

            return AliasedComponent{std::move(values)};
        }
    };
}

}

TEST_CASE("test_component_codec") {
    World world;
    auto type = world.register_component<AliasedComponent>("example.Aliased", 1, aliased_codec());
    auto callerValues = std::make_shared<std::vector<std::int64_t>>(
        std::initializer_list<std::int64_t>{10, 20}
    );

    world.add_component(type, "shared", AliasedComponent{callerValues});
    BehaviorId behavior = world.create_behavior();
    world.grant_component_access(type, behavior, "shared", ComponentAccessMode::ReadWrite);
    ComponentSlotId slot = world.get_components(type, behavior).at("shared");

    IntentId intent = world.create_intent(
        behavior,
        type,
        slot,
        IntentLifetime::persistent(),
        AliasedComponent{callerValues}
    );

    callerValues->at(0) = 99;
    callerValues->push_back(30);

    const liquid::Value::Array& snapshot = world.intent(intent).encodedValue.as_array();
    REQUIRE(snapshot.size() == 2);
    REQUIRE(snapshot.at(0).as_signed_integer() == 10);
    REQUIRE(snapshot.at(1).as_signed_integer() == 20);

    const auto& schema = world.component_schema(type);
    REQUIRE(schema.name == "example.Aliased");
    REQUIRE(schema.version == 1);

    AliasedComponent decoded = world.decode_component(type, world.intent(intent).encodedValue);
    REQUIRE(decoded.values->at(0) == 10);

    world.clear_component_mutations();
    world.update_component(type, behavior, "shared", [](AliasedComponent& component) {
        component.values->at(0) = 40;
    });
    REQUIRE(world.read_component(type, behavior, "shared")->values->at(0) == 40);
    REQUIRE(world.component_mutations().size() == 1);
    REQUIRE(world.component_mutations().back().before.as_array().at(0).as_signed_integer() == 99);
    REQUIRE(world.component_mutations().back().after.as_array().at(0).as_signed_integer() == 40);

    bool rejected = false;
    try {
        world.replace_component(type, behavior, "shared", AliasedComponent{nullptr});
    } catch (const std::exception&) {
        rejected = true;
    }
    REQUIRE(rejected);
    REQUIRE(world.read_component(type, behavior, "shared")->values->at(0) == 40);
    REQUIRE(world.component_mutations().size() == 1);

    world.register_effect_codec(type, liquid::EffectCodec<AliasedComponent>{
        liquid::AdapterRoute{"example.values"},
        [](const ComponentName& name, const AliasedComponent& component)
            -> std::optional<liquid::ResolvedEffect> {
            return liquid::ResolvedEffect{
                liquid::AdapterRoute{"example.values"},
                liquid::EffectTarget{name},
                liquid::Value{static_cast<std::int64_t>(component.values->at(0))}
            };
        }
    });
    const auto effect = world.resolve_effect(type, behavior, "shared");
    REQUIRE(effect.has_value());
    REQUIRE(effect->adapterRoute == liquid::AdapterRoute{"example.values"});
    REQUIRE(effect->target == liquid::EffectTarget{"shared"});
    REQUIRE(effect->desiredValue.as_signed_integer() == 40);

}

TEST_CASE("effect codecs cannot change their registered adapter route") {
    World world;
    auto type = world.register_component<AliasedComponent>(
        "example.Aliased", 1, aliased_codec());
    world.add_component(type, "shared", AliasedComponent{
        std::make_shared<std::vector<std::int64_t>>(
            std::initializer_list<std::int64_t>{10})});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(
        type, behavior, "shared", ComponentAccessMode::Read);
    world.register_effect_codec(type, liquid::EffectCodec<AliasedComponent>{
        liquid::AdapterRoute{"example.stable"},
        [](const ComponentName& name, const AliasedComponent& component)
            -> std::optional<liquid::ResolvedEffect> {
            return liquid::ResolvedEffect{
                liquid::AdapterRoute{"example.changed"},
                liquid::EffectTarget{name},
                liquid::Value{component.values->at(0)}
            };
        }
    });

    bool rejected = false;
    try {
        static_cast<void>(world.resolve_effect(type, behavior, "shared"));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    REQUIRE(rejected);
}
