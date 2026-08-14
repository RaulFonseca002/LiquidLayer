#include "liquid/Runtime.hpp"

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <thread>
#include <atomic>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace liquid;

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
    static constexpr std::string_view stableName = "tests.test.runtime.cpp.FrameCaptureSystem";
    static constexpr std::uint32_t version = 1;
    std::vector<CapturedFrame> frames;

    void run(World& world, FrameNumber frame, IntentTime now) override {
        (void)world;
        frames.push_back({frame, now});
    }
};

struct FirstOrderSystem : System {
    static constexpr std::string_view stableName = "tests.test.runtime.cpp.FirstOrderSystem";
    static constexpr std::uint32_t version = 1;
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
    static constexpr std::string_view stableName = "tests.test.runtime.cpp.SecondOrderSystem";
    static constexpr std::uint32_t version = 1;
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

template <int PhaseIndex>
struct PhaseOrderSystem : System {
    static constexpr std::string_view stableName =
        PhaseIndex == 0 ? "tests.runtime.InputPhase" :
        PhaseIndex == 1 ? "tests.runtime.BehaviorPhase" :
            "tests.runtime.DecisionPhase";
    static constexpr std::uint32_t version = 1;
    std::vector<int>* order = nullptr;

    explicit PhaseOrderSystem(std::vector<int>* executionOrder)
        : order(executionOrder) {
    }

    void run(World&, FrameNumber, IntentTime) override {
        order->push_back(PhaseIndex);
    }
};

struct IntentCreatingSystem : System {
    static constexpr std::string_view stableName = "tests.test.runtime.cpp.IntentCreatingSystem";
    static constexpr std::uint32_t version = 1;
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

        if (created)
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
    static constexpr std::string_view stableName = "tests.test.runtime.cpp.LateRegisteredSystem";
    static constexpr std::uint32_t version = 1;
};

struct RegisteringDuringRunSystem : System {
    static constexpr std::string_view stableName = "tests.test.runtime.cpp.RegisteringDuringRunSystem";
    static constexpr std::uint32_t version = 1;
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
    static constexpr std::string_view stableName = "tests.test.runtime.cpp.SelfDestroyingSystem";
    static constexpr std::uint32_t version = 1;
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
    static constexpr std::string_view stableName = "tests.test.runtime.cpp.ComponentRemovingSystem";
    static constexpr std::uint32_t version = 1;
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
    static constexpr std::string_view stableName = "tests.test.runtime.cpp.ThrowingSystem";
    static constexpr std::uint32_t version = 1;
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
    static constexpr std::string_view stableName = "tests.test.runtime.cpp.ReentrantRuntimeSystem";
    static constexpr std::uint32_t version = 1;
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

    REQUIRE(thrown);
}

std::map<ComponentTypeId, std::map<ComponentName, ComponentSlotId>> resolutions_for(
    ComponentType<Light> type,
    ComponentSlotId slot
) {
    return {{type.id, {{"officeLight", slot}}}};
}

TEST_CASE("test_runtime")
{
    {
        Runtime exhausted(std::numeric_limits<FrameNumber>::max());
        expect_throw<std::overflow_error>([&] { exhausted.run_frame(0); });
        REQUIRE(!exhausted.faulted());
    }

    {
        Runtime runtime;
        std::atomic<bool> rejected{false};
        std::thread other([&] {
            try {
                (void)runtime.world();
            } catch (const std::logic_error&) {
                rejected = true;
            }
        });
        other.join();
        REQUIRE(rejected);
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        const auto lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "office", Light{10});
        std::atomic<bool> rejected{false};
        std::thread other([&] {
            try {
                static_cast<void>(world.get_component_named(lightType, "office"));
            } catch (const std::logic_error&) {
                rejected = true;
            }
        });
        other.join();
        REQUIRE(rejected);
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        world.register_system<FrameCaptureSystem>(Signature{});

        FrameLog first = runtime.run_frame(10);
        FrameLog second = runtime.run_frame(20);

        auto& system = world.get_system<FrameCaptureSystem>();

        REQUIRE(first.frame == 0);
        REQUIRE(first.now == 10);
        REQUIRE(second.frame == 1);
        REQUIRE(second.now == 20);
        REQUIRE(first.completed);
        REQUIRE(second.completed);
        REQUIRE(runtime.frame() == 2);
        REQUIRE(system.frames.size() == 2);
        REQUIRE(system.frames[0].frame == 0);
        REQUIRE(system.frames[0].now == 10);
        REQUIRE(system.frames[1].frame == 1);
        REQUIRE(system.frames[1].now == 20);

        std::vector<std::string> expectedPhases{
            "begin_frame",
            "expire_intents",
            "run_input_systems",
            "run_behavior_systems",
            "run_decision_systems",
            "resolve_intents",
            "end_frame"
        };
        REQUIRE(first.phases == expectedPhases);
        REQUIRE(runtime.last_frame_log().frame == 1);
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

        REQUIRE(world.intent_exists(persistent));
        REQUIRE(!world.intent_exists(expired));
        REQUIRE(log.expired_intents == 1);
        REQUIRE(log.resolution_requests == 1);
        REQUIRE(log.selected_intents == 1);
        REQUIRE(log.intent_selections.size() == 1);
        REQUIRE(log.intent_selections.at(lightType.id).at("officeLight") == persistent);
        REQUIRE(world.get_component_named(lightType, "officeLight")->level == 0);
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        std::vector<std::string> order;

        world.register_system<FirstOrderSystem>(Signature{}, &order);
        world.register_system<SecondOrderSystem>(Signature{}, &order);

        FrameLog log = runtime.run_frame(2);

        REQUIRE(log.systems_run == 2);
        REQUIRE((order == std::vector<std::string>{"first:0", "second:0"}));
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        std::vector<int> order;
        world.register_system<PhaseOrderSystem<2>>(
            Signature{}, SystemPhase::Decision, &order);
        world.register_system<PhaseOrderSystem<0>>(
            Signature{}, SystemPhase::Input, &order);
        world.register_system<PhaseOrderSystem<1>>(
            Signature{}, SystemPhase::Behavior, &order);

        const FrameLog log = runtime.run_frame(3);
        REQUIRE(log.systems_run == 3);
        REQUIRE(order == std::vector<int>{0, 1, 2});
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

        REQUIRE(system.created);
        REQUIRE(world.intent_exists(system.created));
        REQUIRE(first.selected_intents == 1);
        REQUIRE(first.intent_selections.at(lightType.id).at("officeLight") == system.created);
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

        REQUIRE(system.created);
        REQUIRE(!world.intent_exists(system.created));
        REQUIRE(frame.expired_intents == 1);
        REQUIRE(frame.selected_intents == 0);
        REQUIRE(frame.intent_selections.at(lightType.id).empty());
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

        REQUIRE(!runtime.faulted());
        REQUIRE(runtime.frame() == 2);
        REQUIRE(system.frames.size() == 2);

        runtime.run_frame(11);
        REQUIRE(runtime.frame() == 3);
        REQUIRE(system.frames.size() == 3);
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

        REQUIRE(runtime.faulted());
        REQUIRE(runtime.frame() == 0);
        REQUIRE(system.runs == 1);
        REQUIRE(!runtime.last_frame_log().completed);
        REQUIRE(runtime.last_frame_log().failure_phase == "run_decision_systems");
        REQUIRE(runtime.last_frame_log().failure_message == "system failed");
        REQUIRE(runtime.last_frame_log().failure_system ==
               "tests.test.runtime.cpp.ThrowingSystem@1");
        REQUIRE(runtime.last_frame_log().systems_run == 1);

        expect_throw<std::logic_error>([&] {
            runtime.run_frame(11);
        });

        REQUIRE(runtime.frame() == 0);
        REQUIRE(system.runs == 1);
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        world.register_system<RegisteringDuringRunSystem>(Signature{});

        runtime.run_frame(10);

        auto& system = world.get_system<RegisteringDuringRunSystem>();
        REQUIRE(system.registrationRejected);
        REQUIRE(!world.system_exists<LateRegisteredSystem>());
        REQUIRE(!runtime.faulted());

        world.register_system<LateRegisteredSystem>(Signature{});
        REQUIRE(world.system_exists<LateRegisteredSystem>());
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        world.register_system<SelfDestroyingSystem>(Signature{});
        auto& system = world.get_system<SelfDestroyingSystem>();

        runtime.run_frame(10);

        REQUIRE(system.destructionRejected);
        REQUIRE(system.reachedEndOfRun);
        REQUIRE(world.system_exists<SelfDestroyingSystem>());
        REQUIRE(!runtime.faulted());
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        world.register_system<ReentrantRuntimeSystem>(Signature{}, runtime);

        runtime.run_frame(10);

        auto& system = world.get_system<ReentrantRuntimeSystem>();
        REQUIRE(system.reentryRejected);
        REQUIRE(runtime.frame() == 1);
        REQUIRE(!runtime.faulted());
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

        REQUIRE(system.removalRejected);
        REQUIRE(world.has_component_named(lightType, "officeLight"));
        REQUIRE(log.intent_selections.at(lightType.id).at("officeLight") == intent);
        REQUIRE(!runtime.faulted());
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
        const ComponentSlotId replacementSlot =
            world.get_components(lightType, behavior).at("kitchenLight");
        REQUIRE(replacementSlot.slot == recycledSlot.slot);
        REQUIRE(replacementSlot.generation > recycledSlot.generation);

        expect_throw<std::invalid_argument>([&] {
            runtime.run_frame(10, staleResolutions);
        });

        REQUIRE(runtime.frame() == 0);
        REQUIRE(!runtime.faulted());
        REQUIRE(world.has_component_named(lightType, "kitchenLight"));
    }

}
