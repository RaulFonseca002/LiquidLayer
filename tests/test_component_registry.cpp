#include "liquid/detail/ComponentRegistry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace liquid;
using liquid::detail::ComponentRegistry;

struct Light {
    int brightness = 0;
};

struct Temperature {
    int celsius = 0;
};

struct Marker {
    int value = 0;
};

template <typename Function>
void expect_throw(Function function)
{
    bool thrown = false;

    try {
        function();
    } catch (...) {
        thrown = true;
    }

    REQUIRE(thrown);
}

TEST_CASE("test_component_registry")
{
    {
        ComponentRegistry registry;

        expect_throw([&] {
            registry.register_component<Light>("");
        });

        ComponentType<Light> lightType = registry.register_component<Light>("Light");
        ComponentType<Temperature> temperatureType = registry.register_component<Temperature>("Temperature");

        REQUIRE(lightType.id == 0);
        REQUIRE(temperatureType.id == 1);
        REQUIRE(registry.component_type("Light") == lightType);
        REQUIRE(registry.component_type(lightType) == lightType.id);

        expect_throw([&] {
            registry.register_component<Light>("Light");
        });

        expect_throw([&] {
            registry.component_type("Missing");
        });
    }

    {
        ComponentRegistry registry;

        for (std::size_t i = 0; i < MaxComponentTypes; ++i) {
            ComponentType<Marker> type = registry.register_component<Marker>("Marker" + std::to_string(i));
            REQUIRE(type.id == i);
        }

        expect_throw([&] {
            registry.register_component<Marker>("Overflow");
        });
    }

    {
        ComponentRegistry registry;

        ComponentType<Light> lightType = registry.register_component<Light>("Light");
        ComponentType<Temperature> temperatureType = registry.register_component<Temperature>("Temperature");

        expect_throw([&] {
            registry.add_component(lightType, "", Light{1});
        });

        registry.add_component(lightType, "officeLight", Light{40});
        registry.add_component(temperatureType, "officeTemperature", Temperature{22});

        REQUIRE(registry.has_component_named(lightType, "officeLight"));
        REQUIRE(!registry.has_component_named(lightType, "missing"));
        REQUIRE(registry.get_component_named(lightType, "officeLight")->brightness == 40);
        REQUIRE(static_cast<const ComponentRegistry&>(registry).get_component_named(lightType, "officeLight")->brightness == 40);

        registry.get_component_named(lightType, "officeLight")->brightness = 45;
        REQUIRE(registry.get_component_named(lightType, "officeLight")->brightness == 45);

        expect_throw([&] {
            registry.add_component(lightType, "officeLight", Light{80});
        });

        expect_throw([&] {
            registry.get_component_named(lightType, "missing");
        });

        expect_throw([&] {
            registry.remove_component(lightType, "missing");
        });

        ComponentType<Temperature> wrongType{lightType.id};
        expect_throw([&] {
            registry.component_type(wrongType);
        });

        expect_throw([&] {
            registry.add_component(wrongType, "badTemperature", Temperature{1});
        });

        REQUIRE(!registry.has_component_named(wrongType, "officeLight"));

        ComponentType<Light> invalidType;
        expect_throw([&] {
            registry.add_component(invalidType, "badLight", Light{1});
        });

        REQUIRE(!registry.has_component_named(invalidType, "badLight"));
        REQUIRE(!registry.can_read(invalidType, 1, "badLight"));
        REQUIRE(!registry.can_write(invalidType, 1, "badLight"));
    }

    {
        ComponentRegistry registry;

        ComponentType<Light> lightType = registry.register_component<Light>("Light");
        ComponentType<Temperature> temperatureType = registry.register_component<Temperature>("Temperature");

        registry.add_component(lightType, "officeLight", Light{40});
        registry.add_component(lightType, "deskLight", Light{70});
        registry.add_component(temperatureType, "officeTemperature", Temperature{22});

        BehaviorId trackingBehavior = 3;
        BehaviorId focusBehavior = 1;
        BehaviorId writeOnlyBehavior = 2;

        registry.grant_access(lightType, trackingBehavior, "officeLight", ComponentAccessMode::ReadWrite);
        registry.grant_access(lightType, focusBehavior, "officeLight", ComponentAccessMode::Read);
        registry.grant_access(lightType, writeOnlyBehavior, "officeLight", ComponentAccessMode::Write);
        registry.grant_access(temperatureType, trackingBehavior, "officeTemperature", ComponentAccessMode::Read);

        auto trackingLights = registry.get_components(lightType, trackingBehavior);
        auto focusLights = registry.get_components(lightType, focusBehavior);
        auto trackingTemperatures = registry.get_components(temperatureType, trackingBehavior);

        REQUIRE(trackingLights.size() == 1);
        REQUIRE(focusLights.size() == 1);
        REQUIRE(trackingTemperatures.size() == 1);
        REQUIRE(trackingLights.at("officeLight").slot == 0);
        REQUIRE(focusLights.at("officeLight").slot == 0);
        REQUIRE(trackingTemperatures.at("officeTemperature").slot == 0);
        REQUIRE(registry.resolve_component(lightType, trackingLights.at("officeLight")) == registry.get_component_named(lightType, "officeLight"));
        REQUIRE(static_cast<const ComponentRegistry&>(registry).resolve_component(lightType, focusLights.at("officeLight")) == registry.get_component_named(lightType, "officeLight"));

        REQUIRE(registry.can_read(lightType, trackingBehavior, "officeLight"));
        REQUIRE(registry.can_write(lightType, trackingBehavior, "officeLight"));
        REQUIRE(registry.can_read(lightType, focusBehavior, "officeLight"));
        REQUIRE(!registry.can_write(lightType, focusBehavior, "officeLight"));
        REQUIRE(!registry.can_read(lightType, writeOnlyBehavior, "officeLight"));
        REQUIRE(registry.can_write(lightType, writeOnlyBehavior, "officeLight"));
        REQUIRE(!registry.can_read(lightType, 99, "officeLight"));
        REQUIRE(!registry.can_write(lightType, 99, "officeLight"));
        REQUIRE(!registry.can_read(lightType, trackingBehavior, "missing"));
        REQUIRE(!registry.can_write(lightType, trackingBehavior, "missing"));

        std::vector<BehaviorId> behaviors = registry.behaviors_with_access(lightType);
        REQUIRE((behaviors == std::vector<BehaviorId>{focusBehavior, writeOnlyBehavior, trackingBehavior}));

        registry.grant_access(lightType, focusBehavior, "officeLight", ComponentAccessMode::Write);

        focusLights = registry.get_components(lightType, focusBehavior);
        REQUIRE(focusLights.size() == 1);
        REQUIRE(!registry.can_read(lightType, focusBehavior, "officeLight"));
        REQUIRE(registry.can_write(lightType, focusBehavior, "officeLight"));

        registry.revoke_access(lightType, focusBehavior, "officeLight");

        REQUIRE(registry.get_components(lightType, focusBehavior).empty());
        REQUIRE(!registry.can_read(lightType, focusBehavior, "officeLight"));
        REQUIRE(!registry.can_write(lightType, focusBehavior, "officeLight"));

        registry.remove_behavior(trackingBehavior);

        REQUIRE(registry.get_components(lightType, trackingBehavior).empty());
        REQUIRE(registry.get_components(temperatureType, trackingBehavior).empty());
        REQUIRE(!registry.can_read(lightType, trackingBehavior, "officeLight"));
        REQUIRE(!registry.can_read(temperatureType, trackingBehavior, "officeTemperature"));
    }

    {
        ComponentRegistry registry;

        ComponentType<Light> lightType = registry.register_component<Light>("Light");
        registry.add_component(lightType, "officeLight", Light{40});
        registry.add_component(lightType, "deskLight", Light{80});

        BehaviorId trackingBehavior = 1;
        BehaviorId focusBehavior = 2;
        registry.grant_access(lightType, trackingBehavior, "officeLight", ComponentAccessMode::ReadWrite);
        registry.grant_access(lightType, focusBehavior, "officeLight", ComponentAccessMode::Read);
        const ComponentSlotId removedSlot =
            registry.component_slot(lightType, "officeLight");

        registry.remove_component(lightType, "officeLight");

        REQUIRE(!registry.has_component_named(lightType, "officeLight"));
        REQUIRE(registry.get_components(lightType, trackingBehavior).empty());
        REQUIRE(registry.get_components(lightType, focusBehavior).empty());
        REQUIRE(registry.behaviors_with_access(lightType).empty());
        expect_throw([&] {
            registry.resolve_component(lightType, removedSlot);
        });

        registry.add_component(lightType, "reusedLight", Light{90});
        auto reused = registry.get_component_named(lightType, "reusedLight");
        REQUIRE(reused != nullptr);
        REQUIRE(reused->brightness == 90);
        const ComponentSlotId reusedSlot =
            registry.component_slot(lightType, "reusedLight");
        REQUIRE(reusedSlot.slot == removedSlot.slot);
        REQUIRE(reusedSlot.generation == removedSlot.generation + 1);
        REQUIRE(reusedSlot != removedSlot);

        registry.grant_access(lightType, trackingBehavior, "reusedLight", ComponentAccessMode::Read);
        auto lights = registry.get_components(lightType, trackingBehavior);
        REQUIRE(lights.size() == 1);
        REQUIRE(lights.at("reusedLight").slot == 0);
    }

}

TEST_CASE("component slot handles reject stale and cross-world use") {
    ComponentRegistry first{11};
    ComponentRegistry second{22};
    const auto firstType = first.register_component<Light>("Light");
    const auto secondType = second.register_component<Light>("Light");
    first.add_component(firstType, "office", Light{40});
    second.add_component(secondType, "office", Light{20});

    const ComponentSlotId firstSlot = first.component_slot(firstType, "office");
    const ComponentSlotId secondSlot = second.component_slot(secondType, "office");
    REQUIRE(firstSlot.world == 11);
    REQUIRE(secondSlot.world == 22);
    REQUIRE(firstSlot != secondSlot);
    expect_throw([&] {
        second.resolve_component(secondType, firstSlot);
    });

    first.remove_component(firstType, "office");
    first.add_component(firstType, "replacement", Light{50});
    const ComponentSlotId replacement =
        first.component_slot(firstType, "replacement");
    REQUIRE(replacement.slot == firstSlot.slot);
    REQUIRE(replacement.generation == firstSlot.generation + 1);
    expect_throw([&] {
        first.resolve_component(firstType, firstSlot);
    });
}

TEST_CASE("exhausted component slot generations retire the physical slot") {
    ComponentRegistry registry{11};
    const auto type = registry.register_component<Light>("Light");
    registry.add_component(type, "last", Light{10});
    const ComponentSlotId exhausted = registry.set_slot_generation_for_test(
        type, "last", std::numeric_limits<std::uint32_t>::max());
    registry.remove_component(type, "last");
    registry.add_component(type, "replacement", Light{20});
    const ComponentSlotId replacement =
        registry.component_slot(type, "replacement");
    REQUIRE(replacement.slot != exhausted.slot);
    expect_throw([&] {
        registry.resolve_component(type, exhausted);
    });
}

namespace {

liquid::ComponentCodec<Light> light_codec() {
    return {
        [](const Light& light) {
            return liquid::Value{static_cast<std::int64_t>(light.brightness)};
        },
        [](const liquid::Value& encoded) {
            return Light{static_cast<int>(encoded.as_signed_integer())};
        }
    };
}

}

TEST_CASE("component registry rejects lookups on unregistered types") {
    ComponentRegistry registry{1};
    const auto lightType = registry.register_component<Light>("Light");
    registry.add_component(lightType, "lamp", Light{10});
    const ComponentSlotId slot = registry.component_slot(lightType, "lamp");
    const ComponentTypeId unknownType =
        static_cast<ComponentTypeId>(lightType.id + 10);

    REQUIRE_THROWS_AS(registry.effect_route(unknownType), std::runtime_error);
    REQUIRE_THROWS_AS(registry.effect_route(lightType.id), std::runtime_error);

    REQUIRE_THROWS_AS(
        registry.encode_effect(unknownType, "lamp", liquid::Value{}),
        std::runtime_error);
    std::optional<liquid::ResolvedEffect> noEffect;
    REQUIRE_NOTHROW(noEffect =
        registry.encode_effect(lightType.id, "lamp", liquid::Value{}));
    REQUIRE(!noEffect.has_value());

    REQUIRE_THROWS_AS(
        registry.decode_observed(unknownType, liquid::Value{}),
        std::runtime_error);
    REQUIRE_THROWS_AS(
        registry.decode_observed(lightType.id, liquid::Value{}),
        std::runtime_error);

    // type_name and named_slot are private ComponentRegistry members; the
    // public name <-> type and name -> slot lookups carry the same contract.
    REQUIRE(registry.component_type("Light") == lightType.id);
    REQUIRE_THROWS_AS(registry.component_type("Missing"), std::runtime_error);
    REQUIRE_THROWS_AS(
        registry.component_slot(ComponentType<Light>{unknownType}, "lamp"),
        std::runtime_error);
    REQUIRE_THROWS_AS(
        registry.component_name(unknownType, slot), std::runtime_error);
    REQUIRE(registry.component_slot(lightType, "lamp") == slot);

    REQUIRE(registry.component_name(lightType.id, slot) == "lamp");
    REQUIRE(registry.component_type_exists(lightType.id));
    REQUIRE(!registry.component_type_exists(unknownType));
    REQUIRE(!registry.has_component_named(
        ComponentType<Light>{unknownType}, "lamp"));

    ComponentRegistry other{2};
    const auto otherType = other.register_component<Light>("Light");
    other.add_component(otherType, "lamp", Light{20});
    const ComponentSlotId foreignSlot = other.component_slot(otherType, "lamp");
    REQUIRE(registry.slot_is_current(lightType.id, slot));
    REQUIRE(!registry.slot_is_current(lightType.id, ComponentSlotId{}));
    REQUIRE(!registry.slot_is_current(lightType.id, foreignSlot));
    REQUIRE(!registry.slot_is_current(unknownType, slot));
}

TEST_CASE("component registry encode and replace require a component codec") {
    ComponentRegistry plain{1};
    const auto plainType = plain.register_component<Light>("Light");
    plain.add_component(plainType, "lamp", Light{10});
    const ComponentSlotId plainSlot = plain.component_slot(plainType, "lamp");

    REQUIRE(!plain.has_component_codec(plainType));
    REQUIRE_THROWS_AS(
        plain.encode_component(plainType.id, plainSlot), std::runtime_error);
    REQUIRE_THROWS_AS(
        plain.replace_component(plainType.id, plainSlot, liquid::Value{
            static_cast<std::int64_t>(20)}),
        std::runtime_error);
    REQUIRE(plain.get_component_named(plainType, "lamp")->brightness == 10);

    ComponentRegistry coded{2};
    const auto codedType =
        coded.register_component<Light>("example.Light", 1, light_codec());
    coded.add_component(codedType, "lamp", Light{10});
    const ComponentSlotId codedSlot = coded.component_slot(codedType, "lamp");

    REQUIRE(coded.has_component_codec(codedType));
    REQUIRE(coded.encode_component(codedType.id, codedSlot) ==
        liquid::Value{static_cast<std::int64_t>(10)});
    const liquid::Value replaced = coded.replace_component(
        codedType.id, codedSlot, liquid::Value{static_cast<std::int64_t>(20)});
    REQUIRE(replaced == liquid::Value{static_cast<std::int64_t>(20)});
    REQUIRE(coded.get_component_named(codedType, "lamp")->brightness == 20);
    REQUIRE(coded.encode_component(codedType.id, codedSlot) ==
        liquid::Value{static_cast<std::int64_t>(20)});
}

TEST_CASE("component registry routes effects and observations through an effect codec") {
    ComponentRegistry registry{1};
    const auto type =
        registry.register_component<Light>("example.Light", 1, light_codec());
    registry.add_component(type, "lamp", Light{10});
    REQUIRE(!registry.has_effect_codec(type));

    registry.register_effect_codec(type, liquid::EffectCodec<Light>{
        liquid::AdapterRoute{"example.light"},
        [](const ComponentName& name, const Light& light)
            -> std::optional<liquid::ResolvedEffect> {
            if (light.brightness < 0)
                return std::nullopt;
            return liquid::ResolvedEffect{
                liquid::AdapterRoute{"example.light"},
                liquid::EffectTarget{name},
                liquid::Value{static_cast<std::int64_t>(light.brightness)}
            };
        },
        [](const liquid::Value& observed) {
            return Light{static_cast<int>(observed.as_signed_integer() * 2)};
        }
    });
    REQUIRE(registry.has_effect_codec(type));
    REQUIRE(registry.effect_route(type.id) == liquid::AdapterRoute{"example.light"});

    const auto byId = registry.encode_effect(
        type.id, "lamp", liquid::Value{static_cast<std::int64_t>(30)});
    REQUIRE(byId.has_value());
    REQUIRE(byId->adapterRoute == liquid::AdapterRoute{"example.light"});
    REQUIRE(byId->target == liquid::EffectTarget{"lamp"});
    REQUIRE(byId->desiredValue == liquid::Value{static_cast<std::int64_t>(30)});

    const auto typed = registry.encode_effect(type, "lamp", Light{40});
    REQUIRE(typed.has_value());
    REQUIRE(typed->desiredValue == liquid::Value{static_cast<std::int64_t>(40)});

    REQUIRE(!registry.encode_effect(
        type.id, "lamp", liquid::Value{static_cast<std::int64_t>(-1)}));

    REQUIRE(registry.decode_observed(
        type.id, liquid::Value{static_cast<std::int64_t>(7)}) ==
        liquid::Value{static_cast<std::int64_t>(14)});

    // Light's component codec encodes signed integers, so an unsigned value
    // decodes but does not re-encode to the same canonical value.
    REQUIRE_THROWS(registry.encode_effect(
        type.id, "lamp", liquid::Value{std::uint64_t{5}}));
}

TEST_CASE("component registry retires a slot whose generation is exhausted") {
    ComponentRegistry registry{1};
    const auto type = registry.register_component<Light>("Light");
    registry.add_component(type, "lamp", Light{10});
    const ComponentSlotId exhausted = registry.set_slot_generation_for_test(
        type, "lamp", std::numeric_limits<std::uint32_t>::max());
    REQUIRE(registry.slot_is_current(type.id, exhausted));

    registry.remove_component(type, "lamp");
    REQUIRE(!registry.slot_is_current(type.id, exhausted));

    registry.add_component(type, "lamp", Light{20});
    const ComponentSlotId fresh = registry.component_slot(type, "lamp");
    REQUIRE(fresh.slot != exhausted.slot);
    REQUIRE(registry.slot_is_current(type.id, fresh));
    REQUIRE(!registry.slot_is_current(type.id, exhausted));
}
