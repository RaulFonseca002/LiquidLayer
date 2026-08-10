#include "liquid/world/World.hpp"
#include "liquid/IntentExpiration.hpp"

#include <cassert>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using ResolutionComponents = std::map<ComponentName, ComponentSlotId>;

template <typename Candidate>
concept HasPublicRunSystems = requires(Candidate& world) {
    world.run_systems(FrameNumber{}, IntentTime{});
};

template <typename Candidate>
concept HasPublicResolveIntents = requires(Candidate& world, const ResolutionComponents& components) {
    world.resolve_intents(ComponentTypeId{}, components, IntentTime{});
};

template <typename Candidate>
concept HasPublicDestroyExpiredIntents = requires(Candidate& world) {
    destroy_expired_intents(world, IntentTime{});
};

template <typename Candidate>
concept HasPublicManualSystemMembership = requires(Candidate& world) {
    world.template add_behavior_to_system<System>(BehaviorId{});
    world.template remove_behavior_from_system<System>(BehaviorId{});
};

static_assert(!std::is_copy_constructible_v<World>);
static_assert(!std::is_copy_assignable_v<World>);
static_assert(!std::is_move_constructible_v<World>);
static_assert(!std::is_move_assignable_v<World>);
static_assert(!std::is_copy_constructible_v<WorldState>);
static_assert(!std::is_move_constructible_v<WorldState>);
static_assert(!std::is_copy_constructible_v<Coordinator>);
static_assert(!std::is_move_constructible_v<Coordinator>);
static_assert(!std::is_constructible_v<Coordinator, WorldState&>);
static_assert(!std::is_copy_constructible_v<ComponentRegistry>);
static_assert(!std::is_move_constructible_v<ComponentRegistry>);
static_assert(!std::is_copy_constructible_v<IntentRegistry>);
static_assert(!std::is_move_constructible_v<IntentRegistry>);
static_assert(!std::is_copy_constructible_v<SystemRegistry>);
static_assert(!std::is_move_constructible_v<SystemRegistry>);
static_assert(!HasPublicRunSystems<World>);
static_assert(!HasPublicResolveIntents<World>);
static_assert(!HasPublicDestroyExpiredIntents<World>);
static_assert(!HasPublicManualSystemMembership<World>);

struct Light {
    int brightness = 0;
};

struct Temperature {
    int celsius = 0;
};

struct TrackingSystem : System {
    std::vector<std::string> events;

    void on_behavior_added(BehaviorId behavior) override {
        events.push_back("+" + std::to_string(behavior));
    }

    void on_behavior_removed(BehaviorId behavior) override {
        events.push_back("-" + std::to_string(behavior));
    }
};

struct TemperatureSystem : System {
    std::vector<BehaviorId> added;
    std::vector<BehaviorId> removed;

    void on_behavior_added(BehaviorId behavior) override {
        added.push_back(behavior);
    }

    void on_behavior_removed(BehaviorId behavior) override {
        removed.push_back(behavior);
    }
};

struct ConfiguredSystem : System {
    int threshold = 0;
    std::string label;

    ConfiguredSystem(int configuredThreshold, std::string configuredLabel)
        : threshold(configuredThreshold),
          label(std::move(configuredLabel))
    {
    }
};

struct ThrowingRemovalSystem : System {
    std::vector<BehaviorId> removed;

    void on_behavior_removed(BehaviorId behavior) override {
        removed.push_back(behavior);
        throw std::runtime_error("removal callback failed");
    }
};

struct ThrowingAdditionSystem : System {
    void on_behavior_added(BehaviorId behavior) override {
        (void)behavior;
        throw std::runtime_error("addition callback failed");
    }
};

struct RemovalObserverSystem : System {
    std::vector<BehaviorId> removed;

    void on_behavior_removed(BehaviorId behavior) override {
        removed.push_back(behavior);
    }
};

struct WorldMutatingCallbackSystem : System {
    World* world = nullptr;
    BehaviorId victim = 0;
    bool armed = false;
    bool mutationRejected = false;

    explicit WorldMutatingCallbackSystem(World& owningWorld)
        : world(&owningWorld)
    {
    }

    void on_behavior_added(BehaviorId behavior) override {
        if (!armed || behavior == victim)
            return;

        try {
            world->destroy_behavior(victim);
        } catch (const std::logic_error&) {
            mutationRejected = true;
        }
    }
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

    assert(thrown);
}

int main()
{
    {
        World world;
        BehaviorId existing = world.create_behavior();

        world.register_system<TrackingSystem>(Signature{});
        assert(world.system_has_behavior<TrackingSystem>(existing));

        BehaviorId createdLater = world.create_behavior();
        assert(world.system_has_behavior<TrackingSystem>(createdLater));
        assert(world.system_behavior_count<TrackingSystem>() == 2);
    }

    {
        World world;
        world.register_system<WorldMutatingCallbackSystem>(Signature{}, world);

        BehaviorId victim = world.create_behavior();
        WorldMutatingCallbackSystem& system = world.get_system<WorldMutatingCallbackSystem>();
        system.victim = victim;
        system.armed = true;

        BehaviorId trigger = world.create_behavior();

        assert(system.mutationRejected);
        assert(world.behavior_exists(victim));
        assert(world.behavior_exists(trigger));
        assert(world.system_has_behavior<WorldMutatingCallbackSystem>(victim));
        assert(world.system_has_behavior<WorldMutatingCallbackSystem>(trigger));
        assert(world.system_behavior_count<WorldMutatingCallbackSystem>() == 2);
    }

    {
        World world;
        world.create_behavior();

        expect_throw([&] {
            world.register_system<ThrowingAdditionSystem>(Signature{});
        });

        assert(!world.system_exists<ThrowingAdditionSystem>());
        assert(world.system_count() == 0);
        assert(world.behavior_count() == 1);
    }

    {
        World world;

        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{40});

        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::ReadWrite);
        ComponentSlotId slot = world.get_components(lightType, behavior).at("officeLight");
        IntentId intent = world.create_intent(behavior, lightType, slot, IntentLifetime::persistent(), Light{80});

        assert(behavior == 0);
        assert(world.behavior_exists(behavior));
        assert(world.behavior_count() == 1);
        assert(world.intent_exists(intent));
        assert(world.intent_owner(intent) == behavior);
        assert(world.intent_target(intent) == (ComponentTarget{lightType.id, slot}));
        assert(world.typed_intent(lightType, intent).value.brightness == 80);
        assert(world.intent_count(behavior) == 1);

        expect_throw([&] {
            world.create_intent(42, lightType, slot, IntentLifetime::persistent(), Light{10});
        });

        expect_throw([&] {
            world.intent_count(42);
        });

        expect_throw([&] {
            world.destroy_behavior(42);
        });

        world.destroy_behavior(behavior);

        assert(!world.behavior_exists(behavior));
        assert(!world.intent_exists(intent));
        assert(world.behavior_count() == 0);

        expect_throw([&] {
            world.intent_count(behavior);
        });

        BehaviorId recycled = world.create_behavior();
        world.grant_component_access(lightType, recycled, "officeLight", ComponentAccessMode::ReadWrite);
        IntentId recreatedIntent = world.create_intent(recycled, lightType, slot, IntentLifetime::persistent(), Light{90});

        assert(recycled == behavior);
        assert(recreatedIntent == intent);
        assert(world.intent_exists(recreatedIntent));
        assert(world.intent_count(recycled) == 1);
    }

    {
        World world;

        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{40});

        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::ReadWrite);

        Signature lightSignature;
        lightSignature.set(lightType.id);
        world.register_system<TrackingSystem>(lightSignature);

        ComponentSlotId slot = world.get_components(lightType, behavior).at("officeLight");
        IntentId intent = world.create_intent(
            behavior,
            lightType,
            slot,
            IntentLifetime::persistent(),
            Light{80}
        );

        expect_throw([&] {
            world.grant_component_access(
                lightType,
                behavior,
                "officeLight",
                static_cast<ComponentAccessMode>(3)
            );
        });

        assert(world.can_write_component(lightType, behavior, "officeLight"));
        assert(world.behavior_signature(behavior).test(lightType.id));
        assert(world.system_has_behavior<TrackingSystem>(behavior));
        assert(world.intent_exists(intent));
        assert(world.intent_count(behavior) == 1);
    }

    {
        World world;

        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{40});

        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::ReadWrite);
        ComponentSlotId slot = world.get_components(lightType, behavior).at("officeLight");
        IntentId intent = world.create_intent(behavior, lightType, slot, IntentLifetime::persistent(), Light{80});

        world.destroy_intent(intent);

        assert(world.behavior_exists(behavior));
        assert(!world.intent_exists(intent));
        assert(world.behavior_count() == 1);
        assert(world.intent_count(behavior) == 0);
    }

    {
        World world;

        ComponentType<Light> lightType = world.register_component<Light>("Light");
        ComponentType<Temperature> temperatureType = world.register_component<Temperature>("Temperature");

        world.add_component(lightType, "officeLight", Light{40});
        world.add_component(lightType, "deskLight", Light{80});
        world.add_component(temperatureType, "officeTemperature", Temperature{22});

        BehaviorId trackingBehavior = world.create_behavior();
        BehaviorId focusBehavior = world.create_behavior();

        Signature lightSignature;
        lightSignature.set(lightType.id);
        world.register_system<TrackingSystem>(lightSignature);

        auto& trackingSystem = world.get_system<TrackingSystem>();
        assert(trackingSystem.events.empty());
        trackingSystem.events.clear();

        expect_throw([&] {
            world.get_components(lightType, 99);
        });

        expect_throw([&] {
            world.grant_component_access(lightType, 99, "officeLight", ComponentAccessMode::Read);
        });

        world.grant_component_access(lightType, trackingBehavior, "officeLight", ComponentAccessMode::ReadWrite);
        world.grant_component_access(lightType, trackingBehavior, "deskLight", ComponentAccessMode::Read);
        world.grant_component_access(lightType, focusBehavior, "officeLight", ComponentAccessMode::Read);
        world.grant_component_access(temperatureType, trackingBehavior, "officeTemperature", ComponentAccessMode::Read);

        auto trackingLights = world.get_components(lightType, trackingBehavior);
        auto focusLights = world.get_components(lightType, focusBehavior);

        assert(trackingLights.size() == 2);
        assert(focusLights.size() == 1);
        assert(trackingLights.at("officeLight") == 0);
        assert(trackingLights.at("deskLight") == 1);
        assert(focusLights.at("officeLight") == 0);
        assert(world.resolve_component(lightType, trackingLights.at("officeLight"))->brightness == 40);
        assert(world.read_component(lightType, focusBehavior, "officeLight")->brightness == 40);
        assert(world.write_component(lightType, trackingBehavior, "officeLight")->brightness == 40);

        expect_throw([&] {
            world.write_component(lightType, focusBehavior, "officeLight");
        });

        expect_throw([&] {
            world.read_component(lightType, 99, "officeLight");
        });

        expect_throw([&] {
            world.revoke_component_access(lightType, 99, "officeLight");
        });

        assert(world.can_read_component(lightType, focusBehavior, "officeLight"));
        assert(!world.can_write_component(lightType, focusBehavior, "officeLight"));
        assert(world.can_write_component(lightType, trackingBehavior, "officeLight"));
        assert(world.behavior_signature(trackingBehavior).test(lightType.id));
        assert(world.behavior_signature(trackingBehavior).test(temperatureType.id));
        assert(world.behavior_signature(focusBehavior).test(lightType.id));
        assert(world.system_behavior_count<TrackingSystem>() == 2);
        assert(world.system_has_behavior<TrackingSystem>(trackingBehavior));
        assert(world.system_has_behavior<TrackingSystem>(focusBehavior));
        assert((trackingSystem.events == std::vector<std::string>{"+0", "+1"}));

        world.remove_component(lightType, "officeLight");

        assert(!world.has_component_named(lightType, "officeLight"));
        assert(world.get_components(lightType, focusBehavior).empty());
        assert(!world.behavior_signature(focusBehavior).test(lightType.id));
        assert(!world.system_has_behavior<TrackingSystem>(focusBehavior));
        assert(world.behavior_signature(trackingBehavior).test(lightType.id));
        assert(world.system_has_behavior<TrackingSystem>(trackingBehavior));
        assert(world.get_components(lightType, trackingBehavior).size() == 1);
        assert(world.get_components(lightType, trackingBehavior).at("deskLight") == 1);

        world.add_component(lightType, "taskLight", Light{55});
        world.grant_component_access(lightType, focusBehavior, "taskLight", ComponentAccessMode::ReadWrite);

        auto reusedLights = world.get_components(lightType, focusBehavior);
        assert(reusedLights.size() == 1);
        assert(reusedLights.at("taskLight") == 0);
        assert(world.system_has_behavior<TrackingSystem>(focusBehavior));
        assert(world.resolve_component(lightType, reusedLights.at("taskLight"))->brightness == 55);

        world.revoke_component_access(lightType, focusBehavior, "taskLight");

        assert(world.get_components(lightType, focusBehavior).empty());
        assert(!world.behavior_signature(focusBehavior).test(lightType.id));
        assert(!world.system_has_behavior<TrackingSystem>(focusBehavior));

        world.remove_component(lightType, "deskLight");

        assert(world.get_components(lightType, trackingBehavior).empty());
        assert(!world.behavior_signature(trackingBehavior).test(lightType.id));
        assert(!world.system_has_behavior<TrackingSystem>(trackingBehavior));
        assert(world.behavior_signature(trackingBehavior).test(temperatureType.id));
    }

    {
        World world;

        ComponentType<Temperature> temperatureType = world.register_component<Temperature>("Temperature");
        world.add_component(temperatureType, "officeTemperature", Temperature{22});

        Signature temperatureSignature;
        temperatureSignature.set(temperatureType.id);
        world.register_system<TemperatureSystem>(temperatureSignature);

        BehaviorId first = world.create_behavior();
        BehaviorId second = world.create_behavior();

        world.grant_component_access(temperatureType, first, "officeTemperature", ComponentAccessMode::ReadWrite);
        world.grant_component_access(temperatureType, second, "officeTemperature", ComponentAccessMode::Read);
        ComponentSlotId temperatureSlot = world.get_components(temperatureType, first).at("officeTemperature");
        IntentId intent = world.create_intent(first, temperatureType, temperatureSlot, IntentLifetime::persistent(), Temperature{24});

        assert(world.system_has_behavior<TemperatureSystem>(first));
        assert(world.system_has_behavior<TemperatureSystem>(second));

        world.destroy_behavior(first);

        assert(!world.behavior_exists(first));
        assert(!world.intent_exists(intent));
        assert(!world.system_has_behavior<TemperatureSystem>(first));
        assert(world.system_has_behavior<TemperatureSystem>(second));

        expect_throw([&] {
            world.get_components(temperatureType, first);
        });

        BehaviorId recycled = world.create_behavior();

        assert(recycled == first);
        assert(world.get_components(temperatureType, recycled).empty());
        assert(!world.behavior_signature(recycled).test(temperatureType.id));
        assert(!world.system_has_behavior<TemperatureSystem>(recycled));
    }

    {
        World world;
        ComponentType<Temperature> temperatureType = world.register_component<Temperature>("Temperature");
        world.add_component(temperatureType, "officeTemperature", Temperature{22});

        Signature signature;
        signature.set(temperatureType.id);
        world.register_system<ThrowingRemovalSystem>(signature);
        world.register_system<RemovalObserverSystem>(signature);

        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(
            temperatureType,
            behavior,
            "officeTemperature",
            ComponentAccessMode::ReadWrite
        );
        ComponentSlotId slot = world.get_components(temperatureType, behavior).at("officeTemperature");
        IntentId intent = world.create_intent(
            behavior,
            temperatureType,
            slot,
            IntentLifetime::persistent(),
            Temperature{24}
        );

        expect_throw([&] {
            world.destroy_behavior(behavior);
        });

        assert(!world.behavior_exists(behavior));
        assert(!world.intent_exists(intent));
        assert(!world.system_has_behavior<ThrowingRemovalSystem>(behavior));
        assert(!world.system_has_behavior<RemovalObserverSystem>(behavior));
        assert((world.get_system<ThrowingRemovalSystem>().removed == std::vector<BehaviorId>{behavior}));
        assert((world.get_system<RemovalObserverSystem>().removed == std::vector<BehaviorId>{behavior}));
    }

    {
        World world;

        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{40});

        BehaviorId reader = world.create_behavior();
        BehaviorId writer = world.create_behavior();

        world.grant_component_access(lightType, reader, "officeLight", ComponentAccessMode::Read);
        world.grant_component_access(lightType, writer, "officeLight", ComponentAccessMode::Write);

        ComponentSlotId slot = world.get_components(lightType, writer).at("officeLight");

        expect_throw([&] {
            world.create_intent(reader, lightType, slot, IntentLifetime::persistent(), Light{10});
        });

        IntentId writerIntent = world.create_intent(writer, lightType, slot, IntentLifetime::persistent(), Light{80});
        assert(world.intent_exists(writerIntent));

        world.grant_component_access(lightType, writer, "officeLight", ComponentAccessMode::Read);

        assert(!world.intent_exists(writerIntent));

        expect_throw([&] {
            world.create_intent(writer, lightType, slot, IntentLifetime::persistent(), Light{90});
        });
    }

    {
        World world;

        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{40});

        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::ReadWrite);

        ComponentSlotId slot = world.get_components(lightType, behavior).at("officeLight");
        IntentId intent = world.create_intent(behavior, lightType, slot, IntentLifetime::persistent(), Light{80});

        world.remove_component(lightType, "officeLight");

        assert(!world.intent_exists(intent));
        assert(world.intents_for(lightType.id, slot).empty());

        world.add_component(lightType, "taskLight", Light{55});
        world.grant_component_access(lightType, behavior, "taskLight", ComponentAccessMode::ReadWrite);

        ComponentSlotId reusedSlot = world.get_components(lightType, behavior).at("taskLight");
        assert(reusedSlot == slot);
        assert(world.intents_for(lightType.id, reusedSlot).empty());
    }

    {
        World world;

        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{40});

        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::ReadWrite);

        ComponentSlotId slot = world.get_components(lightType, behavior).at("officeLight");
        IntentId intent = world.create_intent(behavior, lightType, slot, IntentLifetime::persistent(), Light{80});

        world.revoke_component_access(lightType, behavior, "officeLight");

        assert(!world.intent_exists(intent));

        expect_throw([&] {
            world.create_intent(behavior, lightType, slot, IntentLifetime::persistent(), Light{90});
        });
    }

    {
        World world;

        world.register_system<ConfiguredSystem>(Signature{}, 4, "automatic");
        auto& configured = world.get_system<ConfiguredSystem>();
        assert(configured.threshold == 4);
        assert(configured.label == "automatic");

        BehaviorId behavior = world.create_behavior();

        assert(world.system_has_behavior<ConfiguredSystem>(behavior));
        assert(world.system_behavior_count<ConfiguredSystem>() == 1);

        world.destroy_behavior(behavior);
        assert(!world.system_has_behavior<ConfiguredSystem>(behavior));

        world.destroy_system<ConfiguredSystem>();
        assert(!world.system_exists<ConfiguredSystem>());
        assert(world.system_count() == 0);
    }

    {
        World world;
        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{40});
        world.add_component(lightType, "kitchenLight", Light{20});

        BehaviorId behavior = world.create_behavior();
        BehaviorAccessRevision createdRevision = world.behavior_access_revision(behavior);

        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::Read);
        BehaviorAccessRevision officeRevision = world.behavior_access_revision(behavior);
        assert(officeRevision > createdRevision);

        world.grant_component_access(lightType, behavior, "kitchenLight", ComponentAccessMode::Read);
        BehaviorAccessRevision kitchenRevision = world.behavior_access_revision(behavior);
        assert(kitchenRevision > officeRevision);

        world.grant_component_access(lightType, behavior, "kitchenLight", ComponentAccessMode::ReadWrite);
        BehaviorAccessRevision modeRevision = world.behavior_access_revision(behavior);
        assert(modeRevision > kitchenRevision);
        assert(world.read_component(lightType, behavior, "kitchenLight") != nullptr);
        assert(world.write_component(lightType, behavior, "kitchenLight") != nullptr);

        world.grant_component_access(lightType, behavior, "kitchenLight", ComponentAccessMode::ReadWrite);
        assert(world.behavior_access_revision(behavior) == modeRevision);

        world.revoke_component_access(lightType, behavior, "officeLight");
        BehaviorAccessRevision revokeRevision = world.behavior_access_revision(behavior);
        assert(revokeRevision > modeRevision);
        assert(world.get_components(lightType, behavior).count("kitchenLight") == 1);

        world.revoke_component_access(lightType, behavior, "officeLight");
        assert(world.behavior_access_revision(behavior) == revokeRevision);

        expect_throw([&] {
            world.grant_component_access(
                lightType,
                behavior,
                "missingLight",
                ComponentAccessMode::Read
            );
        });
        assert(world.behavior_access_revision(behavior) == revokeRevision);

        expect_throw([&] {
            world.grant_component_access(
                lightType,
                behavior,
                "kitchenLight",
                static_cast<ComponentAccessMode>(255)
            );
        });
        assert(world.behavior_access_revision(behavior) == revokeRevision);
        assert(world.read_component(lightType, behavior, "kitchenLight") != nullptr);
        assert(world.write_component(lightType, behavior, "kitchenLight") != nullptr);
    }

    {
        World world;
        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{40});
        world.add_component(lightType, "kitchenLight", Light{20});

        BehaviorId first = world.create_behavior();
        BehaviorId second = world.create_behavior();
        BehaviorId unaffected = world.create_behavior();
        world.grant_component_access(lightType, first, "officeLight", ComponentAccessMode::Read);
        world.grant_component_access(lightType, first, "kitchenLight", ComponentAccessMode::Read);
        world.grant_component_access(lightType, second, "officeLight", ComponentAccessMode::Write);
        world.grant_component_access(lightType, unaffected, "kitchenLight", ComponentAccessMode::Read);

        BehaviorAccessRevision firstBefore = world.behavior_access_revision(first);
        BehaviorAccessRevision secondBefore = world.behavior_access_revision(second);
        BehaviorAccessRevision unaffectedBefore = world.behavior_access_revision(unaffected);

        world.remove_component(lightType, "officeLight");

        BehaviorAccessRevision firstAfter = world.behavior_access_revision(first);
        BehaviorAccessRevision secondAfter = world.behavior_access_revision(second);
        assert(firstAfter > firstBefore);
        assert(secondAfter > secondBefore);
        assert(firstAfter != secondAfter);
        assert(world.behavior_access_revision(unaffected) == unaffectedBefore);
        assert(world.get_components(lightType, first).count("kitchenLight") == 1);
        assert(world.get_components(lightType, second).empty());
    }

    {
        World world;
        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{40});

        BehaviorId behavior = world.create_behavior();
        Signature lightSignature;
        lightSignature.set(lightType.id);
        world.register_system<ThrowingAdditionSystem>(lightSignature);

        BehaviorAccessRevision before = world.behavior_access_revision(behavior);
        expect_throw([&] {
            world.grant_component_access(
                lightType,
                behavior,
                "officeLight",
                ComponentAccessMode::Read
            );
        });

        assert(world.behavior_access_revision(behavior) > before);
        assert(world.get_components(lightType, behavior).count("officeLight") == 1);
        assert(world.system_has_behavior<ThrowingAdditionSystem>(behavior));
    }

    {
        World world;
        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{40});

        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::Read);

        Signature lightSignature;
        lightSignature.set(lightType.id);
        world.register_system<ThrowingRemovalSystem>(lightSignature);

        BehaviorAccessRevision before = world.behavior_access_revision(behavior);
        expect_throw([&] {
            world.remove_component(lightType, "officeLight");
        });

        assert(world.behavior_access_revision(behavior) > before);
        assert(!world.has_component_named(lightType, "officeLight"));
        assert(!world.system_has_behavior<ThrowingRemovalSystem>(behavior));
        assert(world.get_system<ThrowingRemovalSystem>().removed.size() == 1);
        assert(world.get_system<ThrowingRemovalSystem>().removed.front() == behavior);
    }

    {
        World world;
        BehaviorId original = world.create_behavior();
        BehaviorAccessRevision originalRevision = world.behavior_access_revision(original);

        world.destroy_behavior(original);
        expect_throw([&] {
            (void)world.behavior_access_revision(original);
        });

        BehaviorId recycled = world.create_behavior();
        BehaviorAccessRevision recycledRevision = world.behavior_access_revision(recycled);
        assert(recycled == original);
        assert(recycledRevision > originalRevision);
    }

    return 0;
}
