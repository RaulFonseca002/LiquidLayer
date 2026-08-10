#include "liquid/IntentExpiration.hpp"
#include "liquid/IntentRegistry.hpp"
#include "liquid/Runtime.hpp"

#include <cassert>
#include <type_traits>

struct Light {
    int brightness = 0;
};

bool contains(const std::vector<IntentId>& ids, IntentId wanted)
{
    for (IntentId id : ids) {
        if (id == wanted)
            return true;
    }

    return false;
}

int main()
{
    {
        static_assert(!std::is_default_constructible_v<IntentLifetime>);

        IntentLifetime persistent = IntentLifetime::persistent();
        IntentLifetime timed = IntentLifetime::until_time(10);

        assert(persistent.kind == IntentLifetimeKind::Persistent);
        assert(timed.kind == IntentLifetimeKind::UntilTime);
        assert(timed.expiresAt == 10);
    }

    {
        IntentRegistry intents;
        BehaviorId owner = 4;
        ComponentType<Light> lightType{2};
        ComponentSlotId slot = 8;

        intents.create_behavior_pool(owner);

        IntentId persistent = intents.create(owner, lightType, slot, IntentLifetime::persistent(), Light{10});
        IntentId timed = intents.create(owner, lightType, slot, IntentLifetime::until_time(20), Light{20});
        IntentId later = intents.create(owner, lightType, slot, IntentLifetime::until_time(30), Light{30});

        assert(intents.size() == 3);
        assert(intents.intents_for(lightType.id, slot).size() == 3);

        std::vector<IntentId> beforeExpiration = expired_intent_ids(intents, 19);
        assert(beforeExpiration.empty());

        std::vector<IntentId> expired = expired_intent_ids(intents, 20);
        assert(expired.size() == 1);
        assert(contains(expired, timed));

        std::size_t destroyed = destroy_expired_intents(intents, 20);
        assert(destroyed == 1);
        assert(intents.exists(persistent));
        assert(!intents.exists(timed));
        assert(intents.exists(later));
        assert(intents.size(owner) == 2);

        intents.destroy(later);
        assert(!intents.exists(later));
        assert(intents.size(owner) == 1);
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        ComponentType<Light> lightType = world.register_component<Light>("Light");

        world.add_component(lightType, "officeLight", Light{50});

        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::ReadWrite);

        ComponentSlotId slot = world.get_components(lightType, behavior).at("officeLight");

        IntentId timed = world.create_intent(behavior, lightType, slot, IntentLifetime::until_time(5), Light{80});
        IntentId persistent = world.create_intent(behavior, lightType, slot, IntentLifetime::persistent(), Light{20});

        assert(world.intent_target(timed) == (ComponentTarget{lightType.id, slot}));
        assert(world.intents_for(lightType.id, slot).size() == 2);
        assert(world.typed_intent(lightType, timed).value.brightness == 80);

        std::vector<IntentId> expired = expired_intent_ids(world, 5);
        assert(expired.size() == 1);
        assert(contains(expired, timed));

        FrameLog log = runtime.run_frame(5);

        assert(log.expired_intents == 1);
        assert(!world.intent_exists(timed));
        assert(world.intent_exists(persistent));

        world.destroy_intent(persistent);
        assert(!world.intent_exists(persistent));
    }

    return 0;
}
