#include "liquid/detail/ComponentRegistry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
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
