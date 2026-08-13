#include "liquid/detail/SystemRegistry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

using namespace liquid;
using liquid::detail::SystemRegistry;

struct TrackingSystem : System {
    static constexpr std::string_view stableName = "tests.test.system.registry.cpp.TrackingSystem";
    static constexpr std::uint32_t version = 1;
    std::vector<std::string> events;

    void on_behavior_added(BehaviorId behavior) override {
        events.push_back("+" + std::to_string(behavior));
    }

    void on_behavior_removed(BehaviorId behavior) override {
        events.push_back("-" + std::to_string(behavior));
    }

    std::size_t exposed_count() const {
        return behaviors().size();
    }

    bool exposes(BehaviorId behavior) const {
        return behaviors().contains(behavior);
    }
};

struct CallbackRegisteredSystem : System {
    static constexpr std::string_view stableName = "tests.test.system.registry.cpp.CallbackRegisteredSystem";
    static constexpr std::uint32_t version = 1;
};

struct RegistryMutatingCallbackSystem : System {
    static constexpr std::string_view stableName = "tests.test.system.registry.cpp.RegistryMutatingCallbackSystem";
    static constexpr std::uint32_t version = 1;
    SystemRegistry& registry;
    bool mutationRejected = false;

    explicit RegistryMutatingCallbackSystem(SystemRegistry& owner)
        : registry(owner)
    {
    }

    void on_behavior_added(BehaviorId behavior) override {
        (void)behavior;

        try {
            registry.register_system<CallbackRegisteredSystem>(Signature{});
        } catch (const std::logic_error&) {
            mutationRejected = true;
        }
    }
};

struct LightingSystem : System {
    static constexpr std::string_view stableName = "tests.test.system.registry.cpp.LightingSystem";
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

struct ClimateSystem : System {
    static constexpr std::string_view stableName = "tests.test.system.registry.cpp.ClimateSystem";
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
    static constexpr std::string_view stableName = "tests.test.system.registry.cpp.ConfiguredSystem";
    static constexpr std::uint32_t version = 1;
    int threshold = 0;
    std::string label;

    ConfiguredSystem(int configuredThreshold, std::string configuredLabel)
        : threshold(configuredThreshold),
          label(std::move(configuredLabel))
    {
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

TEST_CASE("test_system_registry")
{
    {
        SystemRegistry systems;

        systems.register_system<TrackingSystem>(Signature{});

        REQUIRE(systems.exists<TrackingSystem>());
        REQUIRE(systems.size() == 1);
        REQUIRE(systems.signature<TrackingSystem>().none());
        REQUIRE(systems.behavior_count<TrackingSystem>() == 0);

        expect_throw([&] {
            systems.register_system<TrackingSystem>(Signature{});
        });
    }

    {
        SystemRegistry systems;

        expect_throw([&] {
            systems.get_system<TrackingSystem>();
        });

        expect_throw([&] {
            systems.signature<TrackingSystem>();
        });

        expect_throw([&] {
            systems.set_signature<TrackingSystem>({});
        });

        expect_throw([&] {
            systems.destroy_system<TrackingSystem>();
        });

        expect_throw([&] {
            systems.add_behavior<TrackingSystem>(1);
        });

        expect_throw([&] {
            systems.remove_behavior<TrackingSystem>(1);
        });

        expect_throw([&] {
            systems.has_behavior<TrackingSystem>(1);
        });
    }

    {
        SystemRegistry systems;

        systems.register_system<ConfiguredSystem>(Signature{}, 8, "focus");

        auto& configured = systems.get_system<ConfiguredSystem>();
        REQUIRE(configured.threshold == 8);
        REQUIRE(configured.label == "focus");
    }

    {
        SystemRegistry systems;

        systems.register_system<TrackingSystem>(Signature{});
        auto& tracking = systems.get_system<TrackingSystem>();

        systems.update_behavior(7, {});
        systems.update_behavior(7, {});

        REQUIRE(systems.has_behavior<TrackingSystem>(7));
        REQUIRE(systems.behavior_count<TrackingSystem>() == 1);
        REQUIRE(tracking.exposed_count() == 1);
        REQUIRE(tracking.exposes(7));
        REQUIRE((tracking.events == std::vector<std::string>{"+7"}));

        systems.remove_behavior<TrackingSystem>(7);
        systems.remove_behavior<TrackingSystem>(7);

        REQUIRE(!systems.has_behavior<TrackingSystem>(7));
        REQUIRE(systems.behavior_count<TrackingSystem>() == 0);
        REQUIRE((tracking.events == std::vector<std::string>{"+7", "-7"}));
    }

    {
        SystemRegistry systems;

        Signature lightSignature;
        lightSignature.set(0);
        Signature temperatureSignature;
        temperatureSignature.set(1);

        systems.register_system<LightingSystem>(lightSignature);
        systems.register_system<ClimateSystem>(temperatureSignature);

        systems.update_behavior(10, lightSignature);

        REQUIRE(systems.has_behavior<LightingSystem>(10));
        REQUIRE(!systems.has_behavior<ClimateSystem>(10));

        Signature both = lightSignature | temperatureSignature;
        systems.update_behavior(10, both);

        REQUIRE(systems.has_behavior<LightingSystem>(10));
        REQUIRE(systems.has_behavior<ClimateSystem>(10));

        systems.update_behavior(10, temperatureSignature);

        REQUIRE(!systems.has_behavior<LightingSystem>(10));
        REQUIRE(systems.has_behavior<ClimateSystem>(10));

        auto& lighting = systems.get_system<LightingSystem>();
        auto& climate = systems.get_system<ClimateSystem>();

        REQUIRE((lighting.added == std::vector<BehaviorId>{10}));
        REQUIRE((lighting.removed == std::vector<BehaviorId>{10}));
        REQUIRE((climate.added == std::vector<BehaviorId>{10}));
        REQUIRE(climate.removed.empty());
    }

    {
        SystemRegistry systems;

        systems.register_system<TrackingSystem>(Signature{});
        auto& tracking = systems.get_system<TrackingSystem>();

        systems.add_behavior<TrackingSystem>(1);
        systems.add_behavior<TrackingSystem>(2);
        systems.add_behavior<TrackingSystem>(1);

        REQUIRE(systems.behavior_count<TrackingSystem>() == 2);
        REQUIRE((tracking.events == std::vector<std::string>{"+1", "+2"}));

        systems.remove_behavior(1);
        systems.remove_behavior(1);

        REQUIRE(!systems.has_behavior<TrackingSystem>(1));
        REQUIRE(systems.has_behavior<TrackingSystem>(2));
        REQUIRE((tracking.events == std::vector<std::string>{"+1", "+2", "-1"}));
    }

    {
        SystemRegistry systems;

        systems.register_system<TrackingSystem>(Signature{});
        systems.add_behavior<TrackingSystem>(1);

        systems.destroy_system<TrackingSystem>();

        REQUIRE(!systems.exists<TrackingSystem>());
        REQUIRE(systems.size() == 0);

        expect_throw([&] {
            systems.get_system<TrackingSystem>();
        });
    }

    {
        SystemRegistry systems;
        systems.register_system<RegistryMutatingCallbackSystem>(Signature{}, systems);

        systems.update_behavior(7, Signature{});

        auto& system = systems.get_system<RegistryMutatingCallbackSystem>();
        REQUIRE(system.mutationRejected);
        REQUIRE(systems.has_behavior<RegistryMutatingCallbackSystem>(7));
        REQUIRE(!systems.exists<CallbackRegisteredSystem>());
    }

}
