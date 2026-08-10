#include "liquid/Runtime.hpp"

#include <cassert>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

static_assert(!std::is_copy_constructible_v<Runtime>);
static_assert(!std::is_copy_assignable_v<Runtime>);
static_assert(!std::is_move_constructible_v<Runtime>);
static_assert(!std::is_move_assignable_v<Runtime>);

struct Light {
    int level = 0;
};

struct CapturedFrame {
    FrameNumber frame = 0;
    IntentTime now = 0;
};

struct FrameCaptureSystem : System {
    std::vector<CapturedFrame> frames;

    void run(World& world, FrameNumber frame, IntentTime now) override {
        (void)world;
        frames.push_back({frame, now});
    }
};

struct FirstOrderSystem : System {
    std::vector<std::string>* order = nullptr;

    explicit FirstOrderSystem(std::vector<std::string>* executionOrder)
        : order(executionOrder)
    {
    }

    void run(World& world, FrameNumber frame, IntentTime now) override {
        (void)world;
        (void)now;
        order->push_back("first:" + std::to_string(frame));
    }
};

struct SecondOrderSystem : System {
    std::vector<std::string>* order = nullptr;

    explicit SecondOrderSystem(std::vector<std::string>* executionOrder)
        : order(executionOrder)
    {
    }

    void run(World& world, FrameNumber frame, IntentTime now) override {
        (void)world;
        (void)now;
        order->push_back("second:" + std::to_string(frame));
    }
};

struct IntentCreatingSystem : System {
    ComponentType<Light> type;
    BehaviorId owner = 0;
    ComponentSlotId slot = InvalidComponentSlotId;
    IntentId created = 0;
    bool expiresImmediately = false;

    IntentCreatingSystem(
        ComponentType<Light> componentType,
        BehaviorId intentOwner,
        ComponentSlotId targetSlot,
        bool shouldExpireImmediately = false
    )
        : type(componentType),
          owner(intentOwner),
          slot(targetSlot),
          expiresImmediately(shouldExpireImmediately)
    {
    }

    void run(World& world, FrameNumber frame, IntentTime now) override {
        (void)frame;

        if (created != 0)
            return;

        created = world.create_intent(
            owner,
            type,
            slot,
            expiresImmediately ? IntentLifetime::until_time(now) : IntentLifetime::persistent(),
            Light{static_cast<int>(now)}
        );
    }
};

struct LateRegisteredSystem : System {
};

struct RegisteringDuringRunSystem : System {
    bool registrationRejected = false;

    void run(World& world, FrameNumber frame, IntentTime now) override;
};

void RegisteringDuringRunSystem::run(World& world, FrameNumber frame, IntentTime now) {
    (void)frame;
    (void)now;

    try {
        world.register_system<LateRegisteredSystem>(Signature{});
    } catch (const std::logic_error&) {
        registrationRejected = true;
    }
}

struct SelfDestroyingSystem : System {
    bool destructionRejected = false;
    bool reachedEndOfRun = false;

    void run(World& world, FrameNumber frame, IntentTime now) override;
};

void SelfDestroyingSystem::run(World& world, FrameNumber frame, IntentTime now) {
    (void)frame;
    (void)now;

    try {
        world.destroy_system<SelfDestroyingSystem>();
    } catch (const std::logic_error&) {
        destructionRejected = true;
    }

    reachedEndOfRun = true;
}

struct ComponentRemovingSystem : System {
    ComponentType<Light> type;
    bool removalRejected = false;

    explicit ComponentRemovingSystem(ComponentType<Light> componentType)
        : type(componentType)
    {
    }

    void run(World& world, FrameNumber frame, IntentTime now) override {
        (void)frame;
        (void)now;

        try {
            world.remove_component(type, "officeLight");
        } catch (const std::logic_error&) {
            removalRejected = true;
        }
    }
};

struct ThrowingSystem : System {
    std::size_t runs = 0;

    void run(World& world, FrameNumber frame, IntentTime now) override {
        (void)world;
        (void)frame;
        (void)now;
        ++runs;
        throw std::runtime_error("system failed");
    }
};

struct ReentrantRuntimeSystem : System {
    Runtime& runtime;
    bool reentryRejected = false;

    explicit ReentrantRuntimeSystem(Runtime& owningRuntime)
        : runtime(owningRuntime)
    {
    }

    void run(World& world, FrameNumber frame, IntentTime now) override {
        (void)world;
        (void)frame;

        try {
            runtime.run_frame(now);
        } catch (const std::logic_error&) {
            reentryRejected = true;
        }
    }
};

template <typename Exception, typename Function>
void expect_throw(Function function)
{
    bool thrown = false;

    try {
        function();
    } catch (const Exception&) {
        thrown = true;
    } catch (...) {
    }

    assert(thrown);
}

std::map<ComponentTypeId, std::map<ComponentName, ComponentSlotId>> resolutions_for(
    ComponentType<Light> type,
    ComponentSlotId slot
) {
    return {{type.id, {{"officeLight", slot}}}};
}

int main()
{
    {
        Runtime runtime;
        World& world = runtime.world();
        world.register_system<FrameCaptureSystem>(Signature{});

        FrameLog first = runtime.run_frame(10);
        FrameLog second = runtime.run_frame(20);

        auto& system = world.get_system<FrameCaptureSystem>();

        assert(first.frame == 0);
        assert(first.now == 10);
        assert(second.frame == 1);
        assert(second.now == 20);
        assert(first.completed);
        assert(second.completed);
        assert(runtime.frame() == 2);
        assert(system.frames.size() == 2);
        assert(system.frames[0].frame == 0);
        assert(system.frames[0].now == 10);
        assert(system.frames[1].frame == 1);
        assert(system.frames[1].now == 20);

        std::vector<std::string> expectedPhases{
            "begin_frame",
            "expire_intents",
            "run_systems",
            "resolve_intents",
            "end_frame"
        };
        assert(first.phases == expectedPhases);
        assert(runtime.last_frame_log().frame == 1);
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{0});

        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::ReadWrite);
        ComponentSlotId slot = world.get_components(lightType, behavior).at("officeLight");

        IntentId persistent = world.create_intent(
            behavior,
            lightType,
            slot,
            IntentLifetime::persistent(),
            Light{10},
            IntentPriority::Low
        );
        IntentId expired = world.create_intent(
            behavior,
            lightType,
            slot,
            IntentLifetime::until_time(5),
            Light{90},
            IntentPriority::High
        );

        FrameLog log = runtime.run_frame(5, resolutions_for(lightType, slot));

        assert(world.intent_exists(persistent));
        assert(!world.intent_exists(expired));
        assert(log.expired_intents == 1);
        assert(log.resolution_requests == 1);
        assert(log.selected_intents == 1);
        assert(log.intent_selections.size() == 1);
        assert(log.intent_selections.at(lightType.id).at("officeLight") == persistent);
        assert(world.get_component_named(lightType, "officeLight")->level == 0);
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        std::vector<std::string> order;

        world.register_system<FirstOrderSystem>(Signature{}, &order);
        world.register_system<SecondOrderSystem>(Signature{}, &order);

        FrameLog log = runtime.run_frame(2);

        assert(log.systems_run == 2);
        assert((order == std::vector<std::string>{"first:0", "second:0"}));
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{0});

        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::ReadWrite);
        ComponentSlotId slot = world.get_components(lightType, behavior).at("officeLight");

        world.register_system<IntentCreatingSystem>(Signature{}, lightType, behavior, slot);

        FrameLog first = runtime.run_frame(42, resolutions_for(lightType, slot));
        auto& system = world.get_system<IntentCreatingSystem>();

        assert(system.created != 0);
        assert(world.intent_exists(system.created));
        assert(first.selected_intents == 1);
        assert(first.intent_selections.at(lightType.id).at("officeLight") == system.created);
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{0});

        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::ReadWrite);
        ComponentSlotId slot = world.get_components(lightType, behavior).at("officeLight");

        world.register_system<IntentCreatingSystem>(Signature{}, lightType, behavior, slot, true);

        FrameLog frame = runtime.run_frame(42, resolutions_for(lightType, slot));
        const auto& system = world.get_system<IntentCreatingSystem>();

        assert(system.created != 0);
        assert(!world.intent_exists(system.created));
        assert(frame.expired_intents == 1);
        assert(frame.selected_intents == 0);
        assert(frame.intent_selections.at(lightType.id).empty());
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        world.register_system<FrameCaptureSystem>(Signature{});

        runtime.run_frame(10);
        runtime.run_frame(10);

        auto& system = world.get_system<FrameCaptureSystem>();
        expect_throw<std::invalid_argument>([&] {
            runtime.run_frame(9);
        });

        assert(!runtime.faulted());
        assert(runtime.frame() == 2);
        assert(system.frames.size() == 2);

        runtime.run_frame(11);
        assert(runtime.frame() == 3);
        assert(system.frames.size() == 3);
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        world.register_system<FrameCaptureSystem>(Signature{});
        world.register_system<ThrowingSystem>(Signature{});
        auto& system = world.get_system<ThrowingSystem>();

        expect_throw<std::runtime_error>([&] {
            runtime.run_frame(10);
        });

        assert(runtime.faulted());
        assert(runtime.frame() == 0);
        assert(system.runs == 1);
        assert(!runtime.last_frame_log().completed);
        assert(runtime.last_frame_log().failure_phase == "run_systems");
        assert(runtime.last_frame_log().failure_message == "system failed");
        assert(runtime.last_frame_log().systems_run == 1);

        expect_throw<std::logic_error>([&] {
            runtime.run_frame(11);
        });

        assert(runtime.frame() == 0);
        assert(system.runs == 1);
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        world.register_system<RegisteringDuringRunSystem>(Signature{});

        runtime.run_frame(10);

        auto& system = world.get_system<RegisteringDuringRunSystem>();
        assert(system.registrationRejected);
        assert(!world.system_exists<LateRegisteredSystem>());
        assert(!runtime.faulted());

        world.register_system<LateRegisteredSystem>(Signature{});
        assert(world.system_exists<LateRegisteredSystem>());
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        world.register_system<SelfDestroyingSystem>(Signature{});
        auto& system = world.get_system<SelfDestroyingSystem>();

        runtime.run_frame(10);

        assert(system.destructionRejected);
        assert(system.reachedEndOfRun);
        assert(world.system_exists<SelfDestroyingSystem>());
        assert(!runtime.faulted());
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        world.register_system<ReentrantRuntimeSystem>(Signature{}, runtime);

        runtime.run_frame(10);

        auto& system = world.get_system<ReentrantRuntimeSystem>();
        assert(system.reentryRejected);
        assert(runtime.frame() == 1);
        assert(!runtime.faulted());
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{0});

        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::ReadWrite);
        ComponentSlotId slot = world.get_components(lightType, behavior).at("officeLight");
        IntentId intent = world.create_intent(
            behavior,
            lightType,
            slot,
            IntentLifetime::persistent(),
            Light{90}
        );
        world.register_system<ComponentRemovingSystem>(Signature{}, lightType);

        FrameLog log = runtime.run_frame(10, resolutions_for(lightType, slot));
        auto& system = world.get_system<ComponentRemovingSystem>();

        assert(system.removalRejected);
        assert(world.has_component_named(lightType, "officeLight"));
        assert(log.intent_selections.at(lightType.id).at("officeLight") == intent);
        assert(!runtime.faulted());
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        ComponentType<Light> lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{0});

        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::ReadWrite);
        ComponentSlotId recycledSlot = world.get_components(lightType, behavior).at("officeLight");
        auto staleResolutions = resolutions_for(lightType, recycledSlot);

        world.remove_component(lightType, "officeLight");
        world.add_component(lightType, "kitchenLight", Light{0});
        world.grant_component_access(lightType, behavior, "kitchenLight", ComponentAccessMode::ReadWrite);
        assert(world.get_components(lightType, behavior).at("kitchenLight") == recycledSlot);

        expect_throw<std::invalid_argument>([&] {
            runtime.run_frame(10, staleResolutions);
        });

        assert(runtime.frame() == 0);
        assert(!runtime.faulted());
        assert(world.has_component_named(lightType, "kitchenLight"));
    }

    return 0;
}
