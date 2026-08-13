#include "liquid/world/World.hpp"
#include "liquid/IntentExpiration.hpp"

#include <catch2/catch_test_macros.hpp>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace liquid;
using liquid::detail::ComponentRegistry;
using liquid::detail::Coordinator;
using liquid::detail::IntentRegistry;
using liquid::detail::SystemRegistry;
using liquid::detail::WorldState;

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
    static constexpr std::string_view stableName = "tests.test.world.cpp.TrackingSystem";
    static constexpr std::uint32_t version = 1;
    std::vector<std::string> events;

    void on_behavior_added(BehaviorId behavior) override {
        events.push_back("+" + std::to_string(behavior));
    }

    void on_behavior_removed(BehaviorId behavior) override {
        events.push_back("-" + std::to_string(behavior));
    }
};

struct TemperatureSystem : System {
    static constexpr std::string_view stableName = "tests.test.world.cpp.TemperatureSystem";
    static constexpr std::uint32_t version = 1;
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
    static constexpr std::string_view stableName = "tests.test.world.cpp.ConfiguredSystem";
    static constexpr std::uint32_t version = 1;
    int threshold = 0;
    std::string label;

    ConfiguredSystem(int configuredThreshold, std::string configuredLabel)
        : threshold(configuredThreshold),
          label(std::move(configuredLabel))
    {
    }
};

struct ThrowingRemovalSystem : System {
    static constexpr std::string_view stableName = "tests.test.world.cpp.ThrowingRemovalSystem";
    static constexpr std::uint32_t version = 1;
    std::vector<BehaviorId> removed;

    void on_behavior_removed(BehaviorId behavior) override {
        removed.push_back(behavior);
        throw std::runtime_error("removal callback failed");
    }
};

struct ThrowingAdditionSystem : System {
    static constexpr std::string_view stableName = "tests.test.world.cpp.ThrowingAdditionSystem";
    static constexpr std::uint32_t version = 1;
    void on_behavior_added(BehaviorId behavior) override {
        (void)behavior;
        throw std::runtime_error("addition callback failed");
    }
};

struct RemovalObserverSystem : System {
    static constexpr std::string_view stableName = "tests.test.world.cpp.RemovalObserverSystem";
    static constexpr std::uint32_t version = 1;
    std::vector<BehaviorId> removed;

    void on_behavior_removed(BehaviorId behavior) override {
        removed.push_back(behavior);
    }
};

struct WorldMutatingCallbackSystem : System {
    static constexpr std::string_view stableName = "tests.test.world.cpp.WorldMutatingCallbackSystem";
    static constexpr std::uint32_t version = 1;
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

    REQUIRE(thrown);
}

TEST_CASE("component creation and removal produce complete encoded evidence") {
    World world;
    ComponentCodec<Light> codec{
        [](const Light& light) {
            if (light.brightness < 0)
                throw std::invalid_argument("brightness must be nonnegative");
            return Value{static_cast<std::int64_t>(light.brightness)};
        },
        [](const Value& value) {
            return Light{static_cast<int>(value.as_signed_integer())};
        }
    };
    const auto lightType = world.register_component<Light>(
        "tests.Light", 1, std::move(codec));

    REQUIRE_THROWS_AS(
        world.add_component(lightType, "invalid", Light{-1}),
        std::invalid_argument);
    REQUIRE(!world.has_component_named(lightType, "invalid"));

    world.add_component(lightType, "office", Light{40});
    REQUIRE(world.component_mutations().size() == 1);
    const auto& added = world.component_mutations().front();
    REQUIRE(!added.removed);
    REQUIRE(added.name == "office");
    REQUIRE(added.before.kind() == Value::Kind::Null);
    REQUIRE(added.after == Value{std::int64_t{40}});

    world.clear_component_mutations();
    world.remove_component(lightType, "office");
    REQUIRE(world.component_mutations().size() == 1);
    const auto& removed = world.component_mutations().front();
    REQUIRE(removed.removed);
    REQUIRE(removed.name == "office");
    REQUIRE(removed.before == Value{std::int64_t{40}});
    REQUIRE(removed.after.kind() == Value::Kind::Null);
}

TEST_CASE("test_world")
{
    {
        World world;
        BehaviorId existing = world.create_behavior();

        world.register_system<TrackingSystem>(Signature{});
        REQUIRE(world.system_has_behavior<TrackingSystem>(existing));

        BehaviorId createdLater = world.create_behavior();
        REQUIRE(world.system_has_behavior<TrackingSystem>(createdLater));
        REQUIRE(world.system_behavior_count<TrackingSystem>() == 2);
    }

    {
        World world;
        world.register_system<WorldMutatingCallbackSystem>(Signature{}, world);

        BehaviorId victim = world.create_behavior();
        WorldMutatingCallbackSystem& system = world.get_system<WorldMutatingCallbackSystem>();
        system.victim = victim;
        system.armed = true;

        BehaviorId trigger = world.create_behavior();

        REQUIRE(system.mutationRejected);
        REQUIRE(world.behavior_exists(victim));
        REQUIRE(world.behavior_exists(trigger));
        REQUIRE(world.system_has_behavior<WorldMutatingCallbackSystem>(victim));
        REQUIRE(world.system_has_behavior<WorldMutatingCallbackSystem>(trigger));
        REQUIRE(world.system_behavior_count<WorldMutatingCallbackSystem>() == 2);
    }

    {
        World world;
        world.create_behavior();

        expect_throw([&] {
            world.register_system<ThrowingAdditionSystem>(Signature{});
        });

        REQUIRE(!world.system_exists<ThrowingAdditionSystem>());
        REQUIRE(world.system_count() == 0);
        REQUIRE(world.behavior_count() == 1);
    }

    {
        World world;

        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{40});

        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::ReadWrite);
        ComponentSlotId slot = world.get_components(lightType, behavior).at("officeLight");
        IntentId intent = world.create_intent(behavior, lightType, slot, IntentLifetime::persistent(), Light{80});

        REQUIRE(behavior.slot == 0);
        REQUIRE(world.behavior_exists(behavior));
        REQUIRE(world.behavior_count() == 1);
        REQUIRE(world.intent_exists(intent));
        REQUIRE(world.intent_owner(intent) == behavior);
        REQUIRE(world.intent_target(intent) == (ComponentTarget{lightType.id, slot}));
        REQUIRE(world.typed_intent(lightType, intent).value.brightness == 80);
        REQUIRE(world.intent_count(behavior) == 1);

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

        REQUIRE(!world.behavior_exists(behavior));
        REQUIRE(!world.intent_exists(intent));
        REQUIRE(world.behavior_count() == 0);

        expect_throw([&] {
            world.intent_count(behavior);
        });

        BehaviorId recycled = world.create_behavior();
        world.grant_component_access(lightType, recycled, "officeLight", ComponentAccessMode::ReadWrite);
        IntentId recreatedIntent = world.create_intent(recycled, lightType, slot, IntentLifetime::persistent(), Light{90});

        REQUIRE(recycled.slot == behavior.slot);
        REQUIRE(recycled.generation > behavior.generation);
        REQUIRE(recreatedIntent.slot == intent.slot);
        REQUIRE(recreatedIntent.generation > intent.generation);
        REQUIRE(world.intent_exists(recreatedIntent));
        REQUIRE(world.intent_count(recycled) == 1);
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

        REQUIRE(world.can_write_component(lightType, behavior, "officeLight"));
        REQUIRE(world.behavior_signature(behavior).test(lightType.id));
        REQUIRE(world.system_has_behavior<TrackingSystem>(behavior));
        REQUIRE(world.intent_exists(intent));
        REQUIRE(world.intent_count(behavior) == 1);
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

        REQUIRE(world.behavior_exists(behavior));
        REQUIRE(!world.intent_exists(intent));
        REQUIRE(world.behavior_count() == 1);
        REQUIRE(world.intent_count(behavior) == 0);
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
        REQUIRE(trackingSystem.events.empty());
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

        REQUIRE(trackingLights.size() == 2);
        REQUIRE(focusLights.size() == 1);
        REQUIRE(trackingLights.at("officeLight").slot == 0);
        REQUIRE(trackingLights.at("deskLight").slot == 1);
        REQUIRE(focusLights.at("officeLight").slot == 0);
        REQUIRE(world.resolve_component(lightType, trackingLights.at("officeLight"))->brightness == 40);
        REQUIRE(world.read_component(lightType, focusBehavior, "officeLight")->brightness == 40);
        REQUIRE(world.can_write_component(lightType, trackingBehavior, "officeLight"));

        REQUIRE(!world.can_write_component(lightType, focusBehavior, "officeLight"));

        expect_throw([&] {
            world.read_component(lightType, 99, "officeLight");
        });

        expect_throw([&] {
            world.revoke_component_access(lightType, 99, "officeLight");
        });

        REQUIRE(world.can_read_component(lightType, focusBehavior, "officeLight"));
        REQUIRE(!world.can_write_component(lightType, focusBehavior, "officeLight"));
        REQUIRE(world.can_write_component(lightType, trackingBehavior, "officeLight"));
        REQUIRE(world.behavior_signature(trackingBehavior).test(lightType.id));
        REQUIRE(world.behavior_signature(trackingBehavior).test(temperatureType.id));
        REQUIRE(world.behavior_signature(focusBehavior).test(lightType.id));
        REQUIRE(world.system_behavior_count<TrackingSystem>() == 2);
        REQUIRE(world.system_has_behavior<TrackingSystem>(trackingBehavior));
        REQUIRE(world.system_has_behavior<TrackingSystem>(focusBehavior));
        REQUIRE((trackingSystem.events == std::vector<std::string>{"+0", "+1"}));

        world.remove_component(lightType, "officeLight");

        REQUIRE(!world.has_component_named(lightType, "officeLight"));
        REQUIRE(world.get_components(lightType, focusBehavior).empty());
        REQUIRE(!world.behavior_signature(focusBehavior).test(lightType.id));
        REQUIRE(!world.system_has_behavior<TrackingSystem>(focusBehavior));
        REQUIRE(world.behavior_signature(trackingBehavior).test(lightType.id));
        REQUIRE(world.system_has_behavior<TrackingSystem>(trackingBehavior));
        REQUIRE(world.get_components(lightType, trackingBehavior).size() == 1);
        REQUIRE(world.get_components(lightType, trackingBehavior).at("deskLight").slot == 1);

        world.add_component(lightType, "taskLight", Light{55});
        world.grant_component_access(lightType, focusBehavior, "taskLight", ComponentAccessMode::ReadWrite);

        auto reusedLights = world.get_components(lightType, focusBehavior);
        REQUIRE(reusedLights.size() == 1);
        REQUIRE(reusedLights.at("taskLight").slot == 0);
        REQUIRE(world.system_has_behavior<TrackingSystem>(focusBehavior));
        REQUIRE(world.resolve_component(lightType, reusedLights.at("taskLight"))->brightness == 55);

        world.revoke_component_access(lightType, focusBehavior, "taskLight");

        REQUIRE(world.get_components(lightType, focusBehavior).empty());
        REQUIRE(!world.behavior_signature(focusBehavior).test(lightType.id));
        REQUIRE(!world.system_has_behavior<TrackingSystem>(focusBehavior));

        world.remove_component(lightType, "deskLight");

        REQUIRE(world.get_components(lightType, trackingBehavior).empty());
        REQUIRE(!world.behavior_signature(trackingBehavior).test(lightType.id));
        REQUIRE(!world.system_has_behavior<TrackingSystem>(trackingBehavior));
        REQUIRE(world.behavior_signature(trackingBehavior).test(temperatureType.id));
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

        REQUIRE(world.system_has_behavior<TemperatureSystem>(first));
        REQUIRE(world.system_has_behavior<TemperatureSystem>(second));

        world.destroy_behavior(first);

        REQUIRE(!world.behavior_exists(first));
        REQUIRE(!world.intent_exists(intent));
        REQUIRE(!world.system_has_behavior<TemperatureSystem>(first));
        REQUIRE(world.system_has_behavior<TemperatureSystem>(second));

        expect_throw([&] {
            world.get_components(temperatureType, first);
        });

        BehaviorId recycled = world.create_behavior();

        REQUIRE(recycled.slot == first.slot);
        REQUIRE(recycled.generation > first.generation);
        REQUIRE(world.get_components(temperatureType, recycled).empty());
        REQUIRE(!world.behavior_signature(recycled).test(temperatureType.id));
        REQUIRE(!world.system_has_behavior<TemperatureSystem>(recycled));
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

        REQUIRE(!world.behavior_exists(behavior));
        REQUIRE(!world.intent_exists(intent));
        REQUIRE(!world.system_has_behavior<ThrowingRemovalSystem>(behavior));
        REQUIRE(!world.system_has_behavior<RemovalObserverSystem>(behavior));
        REQUIRE((world.get_system<ThrowingRemovalSystem>().removed == std::vector<BehaviorId>{behavior}));
        REQUIRE((world.get_system<RemovalObserverSystem>().removed == std::vector<BehaviorId>{behavior}));
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
        REQUIRE(world.intent_exists(writerIntent));

        world.grant_component_access(lightType, writer, "officeLight", ComponentAccessMode::Read);

        REQUIRE(!world.intent_exists(writerIntent));

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

        REQUIRE(!world.intent_exists(intent));
        REQUIRE(world.intents_for(lightType.id, slot).empty());

        world.add_component(lightType, "taskLight", Light{55});
        world.grant_component_access(lightType, behavior, "taskLight", ComponentAccessMode::ReadWrite);

        ComponentSlotId reusedSlot = world.get_components(lightType, behavior).at("taskLight");
        REQUIRE(reusedSlot.slot == slot.slot);
        REQUIRE(reusedSlot.generation == slot.generation + 1);
        REQUIRE(reusedSlot != slot);
        REQUIRE(world.intents_for(lightType.id, reusedSlot).empty());
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

        REQUIRE(!world.intent_exists(intent));

        expect_throw([&] {
            world.create_intent(behavior, lightType, slot, IntentLifetime::persistent(), Light{90});
        });
    }

    {
        World world;

        world.register_system<ConfiguredSystem>(Signature{}, 4, "automatic");
        auto& configured = world.get_system<ConfiguredSystem>();
        REQUIRE(configured.threshold == 4);
        REQUIRE(configured.label == "automatic");

        BehaviorId behavior = world.create_behavior();

        REQUIRE(world.system_has_behavior<ConfiguredSystem>(behavior));
        REQUIRE(world.system_behavior_count<ConfiguredSystem>() == 1);

        world.destroy_behavior(behavior);
        REQUIRE(!world.system_has_behavior<ConfiguredSystem>(behavior));

        world.destroy_system<ConfiguredSystem>();
        REQUIRE(!world.system_exists<ConfiguredSystem>());
        REQUIRE(world.system_count() == 0);
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
        REQUIRE(officeRevision > createdRevision);

        world.grant_component_access(lightType, behavior, "kitchenLight", ComponentAccessMode::Read);
        BehaviorAccessRevision kitchenRevision = world.behavior_access_revision(behavior);
        REQUIRE(kitchenRevision > officeRevision);

        world.grant_component_access(lightType, behavior, "kitchenLight", ComponentAccessMode::ReadWrite);
        BehaviorAccessRevision modeRevision = world.behavior_access_revision(behavior);
        REQUIRE(modeRevision > kitchenRevision);
        REQUIRE(world.read_component(lightType, behavior, "kitchenLight") != nullptr);
        REQUIRE(world.can_write_component(lightType, behavior, "kitchenLight"));

        world.grant_component_access(lightType, behavior, "kitchenLight", ComponentAccessMode::ReadWrite);
        REQUIRE(world.behavior_access_revision(behavior) == modeRevision);

        world.revoke_component_access(lightType, behavior, "officeLight");
        BehaviorAccessRevision revokeRevision = world.behavior_access_revision(behavior);
        REQUIRE(revokeRevision > modeRevision);
        REQUIRE(world.get_components(lightType, behavior).count("kitchenLight") == 1);

        world.revoke_component_access(lightType, behavior, "officeLight");
        REQUIRE(world.behavior_access_revision(behavior) == revokeRevision);

        expect_throw([&] {
            world.grant_component_access(
                lightType,
                behavior,
                "missingLight",
                ComponentAccessMode::Read
            );
        });
        REQUIRE(world.behavior_access_revision(behavior) == revokeRevision);

        expect_throw([&] {
            world.grant_component_access(
                lightType,
                behavior,
                "kitchenLight",
                static_cast<ComponentAccessMode>(255)
            );
        });
        REQUIRE(world.behavior_access_revision(behavior) == revokeRevision);
        REQUIRE(world.read_component(lightType, behavior, "kitchenLight") != nullptr);
        REQUIRE(world.can_write_component(lightType, behavior, "kitchenLight"));
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
        REQUIRE(firstAfter > firstBefore);
        REQUIRE(secondAfter > secondBefore);
        REQUIRE(firstAfter != secondAfter);
        REQUIRE(world.behavior_access_revision(unaffected) == unaffectedBefore);
        REQUIRE(world.get_components(lightType, first).count("kitchenLight") == 1);
        REQUIRE(world.get_components(lightType, second).empty());
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

        REQUIRE(world.behavior_access_revision(behavior) > before);
        REQUIRE(world.get_components(lightType, behavior).count("officeLight") == 1);
        REQUIRE(world.system_has_behavior<ThrowingAdditionSystem>(behavior));
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

        REQUIRE(world.behavior_access_revision(behavior) > before);
        REQUIRE(!world.has_component_named(lightType, "officeLight"));
        REQUIRE(!world.system_has_behavior<ThrowingRemovalSystem>(behavior));
        REQUIRE(world.get_system<ThrowingRemovalSystem>().removed.size() == 1);
        REQUIRE(world.get_system<ThrowingRemovalSystem>().removed.front() == behavior);
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
        REQUIRE(recycled.slot == original.slot);
        REQUIRE(recycled.generation > original.generation);
        REQUIRE(recycledRevision > originalRevision);
    }

}
