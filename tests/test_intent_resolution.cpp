#include "liquid/detail/IntentRegistry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <map>

using namespace liquid;
using liquid::detail::IntentRegistry;

struct Light {
    int brightness = 0;
};

TEST_CASE("test_intent_resolution")
{
    {
        IntentRegistry intents;

        ComponentType<Light> lightType{0};
        ComponentSlotId officeSlot = 4;
        BehaviorId owner = 7;

        intents.create_behavior_pool(owner);

        IntentId low = intents.create(owner, lightType, officeSlot, IntentLifetime::persistent(), Light{10}, IntentPriority::Low);
        IntentId medium = intents.create(owner, lightType, officeSlot, IntentLifetime::persistent(), Light{50});
        IntentId high = intents.create(owner, lightType, officeSlot, IntentLifetime::persistent(), Light{90}, IntentPriority::High);

        std::map<ComponentName, ComponentSlotId> components{
            {"officeLight", officeSlot}
        };

        std::map<ComponentName, IntentId> selected = intents.resolve(lightType.id, components, 0);

        REQUIRE(selected.size() == 1);
        REQUIRE(selected.at("officeLight") == high);
        REQUIRE(intents.intent(medium).priority == IntentPriority::Medium);
        REQUIRE(intents.exists(low));
        REQUIRE(intents.exists(medium));
        REQUIRE(intents.exists(high));
    }

    {
        IntentRegistry intents;

        ComponentType<Light> lightType{0};
        ComponentSlotId officeSlot = 1;
        BehaviorId owner = 1;

        intents.create_behavior_pool(owner);

        IntentId older = intents.create(owner, lightType, officeSlot, IntentLifetime::persistent(), Light{10}, IntentPriority::Medium);
        IntentId newer = intents.create(owner, lightType, officeSlot, IntentLifetime::persistent(), Light{90}, IntentPriority::Medium);
        REQUIRE(newer > older);

        std::map<ComponentName, ComponentSlotId> components{
            {"officeLight", officeSlot}
        };

        std::map<ComponentName, IntentId> selected = intents.resolve(lightType.id, components, 0);

        REQUIRE(selected.size() == 1);
        REQUIRE(selected.at("officeLight") == newer);
    }

    {
        IntentRegistry intents;

        ComponentType<Light> lightType{0};
        ComponentSlotId officeSlot = 1;
        BehaviorId owner = 1;

        intents.create_behavior_pool(owner);

        IntentId lower = intents.create(
            owner,
            lightType,
            officeSlot,
            IntentLifetime::persistent(),
            Light{10},
            IntentPriority::Medium
        );
        IntentId higher = intents.create(
            owner,
            lightType,
            officeSlot,
            IntentLifetime::persistent(),
            Light{20},
            IntentPriority::Medium
        );

        intents.destroy(lower);
        IntentId recycled = intents.create(
            owner,
            lightType,
            officeSlot,
            IntentLifetime::persistent(),
            Light{30},
            IntentPriority::Medium
        );

        REQUIRE(recycled.slot == lower.slot);
        REQUIRE(recycled.generation > lower.generation);
        REQUIRE(higher != recycled);

        std::map<ComponentName, ComponentSlotId> components{
            {"officeLight", officeSlot}
        };
        std::map<ComponentName, IntentId> selected = intents.resolve(lightType.id, components, 0);

        REQUIRE(selected.size() == 1);
        REQUIRE(selected.at("officeLight") == recycled);
    }

    {
        IntentRegistry intents;

        ComponentType<Light> lightType{0};
        ComponentSlotId officeSlot = 2;
        BehaviorId owner = 2;

        intents.create_behavior_pool(owner);

        IntentId expiredHigh = intents.create(owner, lightType, officeSlot, IntentLifetime::until_time(5), Light{100}, IntentPriority::High);
        IntentId persistentLow = intents.create(owner, lightType, officeSlot, IntentLifetime::persistent(), Light{20}, IntentPriority::Low);

        std::map<ComponentName, ComponentSlotId> components{
            {"officeLight", officeSlot}
        };

        std::map<ComponentName, IntentId> selected = intents.resolve(lightType.id, components, 5);

        REQUIRE(!intents.exists(expiredHigh));
        REQUIRE(intents.exists(persistentLow));
        REQUIRE(selected.size() == 1);
        REQUIRE(selected.at("officeLight") == persistentLow);
    }

    {
        IntentRegistry intents;

        ComponentType<Light> lightType{0};
        ComponentSlotId officeSlot = 3;
        ComponentSlotId deskSlot = 8;
        BehaviorId owner = 3;

        intents.create_behavior_pool(owner);

        IntentId canceled = intents.create(owner, lightType, officeSlot, IntentLifetime::persistent(), Light{100}, IntentPriority::High);
        IntentId live = intents.create(owner, lightType, officeSlot, IntentLifetime::persistent(), Light{20}, IntentPriority::Low);

        intents.destroy(canceled);

        std::map<ComponentName, ComponentSlotId> components{
            {"officeLight", officeSlot},
            {"deskLight", deskSlot}
        };

        std::map<ComponentName, IntentId> selected = intents.resolve(lightType.id, components, 0);

        REQUIRE(selected.size() == 1);
        REQUIRE(selected.at("officeLight") == live);
        REQUIRE(!selected.contains("deskLight"));
    }

    {
        IntentRegistry intents;

        ComponentType<Light> lightType{0};
        ComponentSlotId officeSlot = 3;
        ComponentSlotId deskSlot = 8;
        BehaviorId owner = 4;

        intents.create_behavior_pool(owner);

        IntentId office = intents.create(owner, lightType, officeSlot, IntentLifetime::persistent(), Light{40}, IntentPriority::Medium);
        IntentId desk = intents.create(owner, lightType, deskSlot, IntentLifetime::persistent(), Light{80}, IntentPriority::Medium);

        std::map<ComponentName, ComponentSlotId> components{
            {"officeLight", officeSlot},
            {"deskLight", deskSlot}
        };

        std::map<ComponentName, IntentId> selected = intents.resolve(lightType.id, components, 0);

        REQUIRE(selected.size() == 2);
        REQUIRE(selected.at("officeLight") == office);
        REQUIRE(selected.at("deskLight") == desk);
    }

}
