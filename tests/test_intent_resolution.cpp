#include "liquid/IntentRegistry.hpp"

#include <cassert>
#include <map>

struct Light {
    int brightness = 0;
};

int main()
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

        assert(selected.size() == 1);
        assert(selected.at("officeLight") == high);
        assert(intents.intent(medium).priority == IntentPriority::Medium);
        assert(intents.exists(low));
        assert(intents.exists(medium));
        assert(intents.exists(high));
    }

    {
        IntentRegistry intents;

        ComponentType<Light> lightType{0};
        ComponentSlotId officeSlot = 1;
        BehaviorId owner = 1;

        intents.create_behavior_pool(owner);

        IntentId older = intents.create(owner, lightType, officeSlot, IntentLifetime::persistent(), Light{10}, IntentPriority::Medium);
        IntentId newer = intents.create(owner, lightType, officeSlot, IntentLifetime::persistent(), Light{90}, IntentPriority::Medium);
        assert(newer > older);

        std::map<ComponentName, ComponentSlotId> components{
            {"officeLight", officeSlot}
        };

        std::map<ComponentName, IntentId> selected = intents.resolve(lightType.id, components, 0);

        assert(selected.size() == 1);
        assert(selected.at("officeLight") == newer);
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

        assert(recycled == lower);

        std::map<ComponentName, ComponentSlotId> components{
            {"officeLight", officeSlot}
        };
        std::map<ComponentName, IntentId> selected = intents.resolve(lightType.id, components, 0);

        assert(selected.size() == 1);
        assert(selected.at("officeLight") == higher);
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

        assert(!intents.exists(expiredHigh));
        assert(intents.exists(persistentLow));
        assert(selected.size() == 1);
        assert(selected.at("officeLight") == persistentLow);
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

        assert(selected.size() == 1);
        assert(selected.at("officeLight") == live);
        assert(!selected.contains("deskLight"));
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

        assert(selected.size() == 2);
        assert(selected.at("officeLight") == office);
        assert(selected.at("deskLight") == desk);
    }

    return 0;
}
