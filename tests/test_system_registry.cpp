#include "liquid/SystemRegistry.hpp"

#include <cassert>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

struct TrackingSystem : System {
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
};

struct RegistryMutatingCallbackSystem : System {
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
        SystemRegistry systems;

        systems.register_system<TrackingSystem>(Signature{});

        assert(systems.exists<TrackingSystem>());
        assert(systems.size() == 1);
        assert(systems.signature<TrackingSystem>().none());
        assert(systems.behavior_count<TrackingSystem>() == 0);

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
        assert(configured.threshold == 8);
        assert(configured.label == "focus");
    }

    {
        SystemRegistry systems;

        systems.register_system<TrackingSystem>(Signature{});
        auto& tracking = systems.get_system<TrackingSystem>();

        systems.update_behavior(7, {});
        systems.update_behavior(7, {});

        assert(systems.has_behavior<TrackingSystem>(7));
        assert(systems.behavior_count<TrackingSystem>() == 1);
        assert(tracking.exposed_count() == 1);
        assert(tracking.exposes(7));
        assert((tracking.events == std::vector<std::string>{"+7"}));

        systems.remove_behavior<TrackingSystem>(7);
        systems.remove_behavior<TrackingSystem>(7);

        assert(!systems.has_behavior<TrackingSystem>(7));
        assert(systems.behavior_count<TrackingSystem>() == 0);
        assert((tracking.events == std::vector<std::string>{"+7", "-7"}));
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

        assert(systems.has_behavior<LightingSystem>(10));
        assert(!systems.has_behavior<ClimateSystem>(10));

        Signature both = lightSignature | temperatureSignature;
        systems.update_behavior(10, both);

        assert(systems.has_behavior<LightingSystem>(10));
        assert(systems.has_behavior<ClimateSystem>(10));

        systems.update_behavior(10, temperatureSignature);

        assert(!systems.has_behavior<LightingSystem>(10));
        assert(systems.has_behavior<ClimateSystem>(10));

        auto& lighting = systems.get_system<LightingSystem>();
        auto& climate = systems.get_system<ClimateSystem>();

        assert((lighting.added == std::vector<BehaviorId>{10}));
        assert((lighting.removed == std::vector<BehaviorId>{10}));
        assert((climate.added == std::vector<BehaviorId>{10}));
        assert(climate.removed.empty());
    }

    {
        SystemRegistry systems;

        systems.register_system<TrackingSystem>(Signature{});
        auto& tracking = systems.get_system<TrackingSystem>();

        systems.add_behavior<TrackingSystem>(1);
        systems.add_behavior<TrackingSystem>(2);
        systems.add_behavior<TrackingSystem>(1);

        assert(systems.behavior_count<TrackingSystem>() == 2);
        assert((tracking.events == std::vector<std::string>{"+1", "+2"}));

        systems.remove_behavior(1);
        systems.remove_behavior(1);

        assert(!systems.has_behavior<TrackingSystem>(1));
        assert(systems.has_behavior<TrackingSystem>(2));
        assert((tracking.events == std::vector<std::string>{"+1", "+2", "-1"}));
    }

    {
        SystemRegistry systems;

        systems.register_system<TrackingSystem>(Signature{});
        systems.add_behavior<TrackingSystem>(1);

        systems.destroy_system<TrackingSystem>();

        assert(!systems.exists<TrackingSystem>());
        assert(systems.size() == 0);

        expect_throw([&] {
            systems.get_system<TrackingSystem>();
        });
    }

    {
        SystemRegistry systems;
        systems.register_system<RegistryMutatingCallbackSystem>(Signature{}, systems);

        systems.update_behavior(7, Signature{});

        auto& system = systems.get_system<RegistryMutatingCallbackSystem>();
        assert(system.mutationRejected);
        assert(systems.has_behavior<RegistryMutatingCallbackSystem>(7));
        assert(!systems.exists<CallbackRegisteredSystem>());
    }

    return 0;
}
