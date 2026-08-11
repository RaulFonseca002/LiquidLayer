#include "liquid/BehaviorRegistry.hpp"
#include "liquid/ComponentRegistry.hpp"
#include "liquid/IntentRegistry.hpp"
#include "liquid/Runtime.hpp"
#include "liquid/scripting/LuaBehaviorRunner.hpp"
#include "liquid/world/World.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

struct StressLight {
    int value = 0;
};

struct StressTemperature {
    int value = 0;
};

struct StressLightSystem : System {
};

struct StressTemperatureSystem : System {
};

struct StressCombinedSystem : System {
};

liquid::scripting::LuaComponentCodec<StressLight> stress_light_codec()
{
    using liquid::scripting::LuaValue;

    return {
        [](const StressLight& light) {
            return LuaValue::Table{{"value", LuaValue{light.value}}};
        },
        [](const LuaValue& value) {
            const auto& table = value.as_table();

            if (table.size() != 1 || !table.contains("value"))
                throw std::runtime_error("StressLight requires exactly value");

            std::int64_t decoded = table.at("value").as_integer();
            if (decoded < 0 || decoded > 100)
                throw std::runtime_error("StressLight value must be between 0 and 100");

            return StressLight{static_cast<int>(decoded)};
        }
    };
}

struct StressLuaSystem : System {
    liquid::scripting::LuaBehaviorRunner* runner;
    BehaviorId owner;
    const std::vector<int>* brightnesses;
    std::size_t successes = 0;
    std::size_t failures = 0;

    StressLuaSystem(
        liquid::scripting::LuaBehaviorRunner& behaviorRunner,
        BehaviorId behavior,
        const std::vector<int>& frameBrightnesses
    )
        : runner(&behaviorRunner), owner(behavior), brightnesses(&frameBrightnesses)
    {
    }

    void run(World& world, FrameNumber frame, IntentTime now) override
    {
        std::string source;
        bool shouldFail = frame % 100 == 99;

        if (shouldFail) {
            source = "error(\"bounded stress failure\", 0)";
        } else if (frame == 0) {
            source =
                "local light = access.StressLight.light; "
                "light.propose({ value = { value = " +
                std::to_string(brightnesses->at(0)) +
                " }, priority = \"low\" }); "
                "light.propose({ value = { value = " +
                std::to_string(brightnesses->at(0)) +
                " }, priority = \"medium\", duration_ms = 1 })";
        } else {
            source =
                "access.StressLight.light.propose({ value = { value = " +
                std::to_string(brightnesses->at(static_cast<std::size_t>(frame))) +
                " }, priority = \"medium\", duration_ms = 1 })";
        }

        liquid::scripting::LuaExecutionResult result = runner->execute(world, owner, now, source);

        if (shouldFail) {
            assert(result.status == liquid::scripting::LuaExecutionStatus::RuntimeError);
            assert(result.createdIntents.empty());
            failures++;
        } else {
            assert(result.succeeded());
            assert(result.createdIntents.size() == (frame == 0 ? 2 : 1));
            successes++;
        }
    }
};

template <typename T>
const T& random_element(const std::vector<T>& values, std::mt19937& rng)
{
    std::uniform_int_distribution<std::size_t> distribution(0, values.size() - 1);
    return values[distribution(rng)];
}

void stress_behavior_registry()
{
    std::mt19937 rng(0xBEEFu);
    BehaviorRegistry registry;
    std::set<BehaviorId> active;
    std::vector<BehaviorId> activeList;

    for (int step = 0; step < 4000; ++step) {
        bool shouldCreate = active.empty() || (rng() % 3 != 0);

        if (shouldCreate && active.size() < MAX_BEHAVIOURS) {
            BehaviorId id = registry.create();
            bool inserted = active.insert(id).second;
            assert(inserted);
            activeList.push_back(id);
        } else if (!activeList.empty()) {
            std::uniform_int_distribution<std::size_t> distribution(0, activeList.size() - 1);
            std::size_t index = distribution(rng);
            BehaviorId id = activeList[index];

            registry.destroy(id);
            active.erase(id);
            activeList[index] = activeList.back();
            activeList.pop_back();
        }

        assert(registry.size() == active.size());

        for (BehaviorId id : active)
            assert(registry.exists(id));

        for (int probe = 0; probe < 8; ++probe) {
            BehaviorId id = static_cast<BehaviorId>(rng() % MAX_BEHAVIOURS);
            assert(registry.exists(id) == active.contains(id));
        }
    }
}

void stress_intent_registry()
{
    std::mt19937 rng(0xFACEu);
    IntentRegistry registry;
    std::array<BehaviorId, 4> owners{1, 2, 3, 4};
    ComponentType<StressLight> type{0};
    std::map<BehaviorId, std::set<IntentId>> active;
    std::map<BehaviorId, std::vector<IntentId>> activeList;

    for (BehaviorId owner : owners) {
        registry.create_behavior_pool(owner);
        active[owner];
        activeList[owner];
    }

    for (int step = 0; step < 5000; ++step) {
        BehaviorId owner = owners[rng() % owners.size()];
        int operation = static_cast<int>(rng() % 10);

        if (operation < 5 && active[owner].size() < MAX_INTENTS) {
            ComponentSlotId slot = static_cast<ComponentSlotId>(rng() % 16);
            IntentId id = registry.create(owner, type, slot, IntentLifetime::persistent(), StressLight{static_cast<int>(step)});
            assert(registry.owner_of(id) == owner);
            assert(registry.target_of(id) == (ComponentTarget{type.id, slot}));
            bool inserted = active[owner].insert(id).second;
            assert(inserted);
            activeList[owner].push_back(id);
        } else if (operation < 8 && !activeList[owner].empty()) {
            std::uniform_int_distribution<std::size_t> distribution(0, activeList[owner].size() - 1);
            std::size_t index = distribution(rng);
            IntentId id = activeList[owner][index];

            registry.destroy(id);
            active[owner].erase(id);
            activeList[owner][index] = activeList[owner].back();
            activeList[owner].pop_back();
        } else {
            registry.destroy_owned_by(owner);
            active[owner].clear();
            activeList[owner].clear();
            registry.create_behavior_pool(owner);
        }

        for (BehaviorId checkedOwner : owners) {
            assert(registry.size(checkedOwner) == active[checkedOwner].size());

            for (IntentId id : active[checkedOwner])
                assert(registry.exists(id));
        }
    }
}

void stress_component_registry()
{
    std::mt19937 rng(0xC0FFEEu);
    ComponentRegistry registry;
    ComponentType<StressLight> type = registry.register_component<StressLight>("StressLight");
    std::vector<std::string> names{"c0", "c1", "c2", "c3", "c4", "c5"};
    std::set<std::string> liveNames;
    std::map<BehaviorId, std::map<std::string, ComponentAccessMode>> modelAccess;
    std::map<std::string, ComponentSlotId> observedSlots;

    for (int step = 0; step < 3000; ++step) {
        const std::string& name = random_element(names, rng);
        BehaviorId behavior = static_cast<BehaviorId>(rng() % 8);
        int operation = static_cast<int>(rng() % 10);

        if (operation < 2) {
            if (!liveNames.contains(name)) {
                registry.add_component(type, name, StressLight{step});
                liveNames.insert(name);
                observedSlots.erase(name);
            }
        } else if (operation < 4) {
            if (liveNames.contains(name)) {
                registry.remove_component(type, name);
                liveNames.erase(name);
                observedSlots.erase(name);

                for (auto& [ignoredBehavior, accesses] : modelAccess) {
                    (void)ignoredBehavior;
                    accesses.erase(name);
                }
            }
        } else if (operation < 8) {
            if (liveNames.contains(name)) {
                ComponentAccessMode mode = ComponentAccessMode::Read;

                if (operation == 6)
                    mode = ComponentAccessMode::Write;
                else if (operation == 7)
                    mode = ComponentAccessMode::ReadWrite;

                registry.grant_access(type, behavior, name, mode);
                modelAccess[behavior][name] = mode;
            }
        } else {
            if (liveNames.contains(name)) {
                registry.revoke_access(type, behavior, name);
                modelAccess[behavior].erase(name);
            }
        }

        for (const std::string& checkedName : names)
            assert(registry.has_component_named(type, checkedName) == liveNames.contains(checkedName));

        std::vector<BehaviorId> expectedBehaviors;

        for (BehaviorId checkedBehavior = 0; checkedBehavior < 8; ++checkedBehavior) {
            auto actual = registry.get_components(type, checkedBehavior);
            std::size_t expectedCount = 0;

            for (const auto& [accessName, mode] : modelAccess[checkedBehavior]) {
                (void)mode;

                if (!liveNames.contains(accessName))
                    continue;

                ++expectedCount;
                assert(actual.contains(accessName));

                ComponentSlotId slot = actual.at(accessName);
                auto observed = observedSlots.find(accessName);

                if (observed == observedSlots.end()) {
                    observedSlots[accessName] = slot;
                } else {
                    assert(observed->second == slot);
                }

                assert(registry.resolve_component(type, slot) != nullptr);

                ComponentAccessMode accessMode = modelAccess[checkedBehavior][accessName];
                bool canRead = accessMode == ComponentAccessMode::Read || accessMode == ComponentAccessMode::ReadWrite;
                bool canWrite = accessMode == ComponentAccessMode::Write || accessMode == ComponentAccessMode::ReadWrite;
                assert(registry.can_read(type, checkedBehavior, accessName) == canRead);
                assert(registry.can_write(type, checkedBehavior, accessName) == canWrite);
            }

            assert(actual.size() == expectedCount);

            if (expectedCount > 0)
                expectedBehaviors.push_back(checkedBehavior);
        }

        auto actualBehaviors = registry.behaviors_with_access(type);
        assert(actualBehaviors == expectedBehaviors);
    }
}

struct WorldModel {
    std::set<BehaviorId> liveBehaviors;
    std::map<BehaviorId, std::set<std::string>> lightAccess;
    std::map<BehaviorId, std::set<std::string>> temperatureAccess;
    std::set<std::string> liveLights;
    std::set<std::string> liveTemperatures;
};

void assert_world_model(
    World& world,
    ComponentType<StressLight> lightType,
    ComponentType<StressTemperature> temperatureType,
    const WorldModel& model)
{
    for (BehaviorId behavior : model.liveBehaviors) {
        auto lightComponents = world.get_components(lightType, behavior);
        auto temperatureComponents = world.get_components(temperatureType, behavior);

        std::set<std::string> expectedLights;
        std::set<std::string> expectedTemperatures;

        auto lightAccess = model.lightAccess.find(behavior);
        if (lightAccess != model.lightAccess.end()) {
            for (const std::string& name : lightAccess->second) {
                if (model.liveLights.contains(name))
                    expectedLights.insert(name);
            }
        }

        auto temperatureAccess = model.temperatureAccess.find(behavior);
        if (temperatureAccess != model.temperatureAccess.end()) {
            for (const std::string& name : temperatureAccess->second) {
                if (model.liveTemperatures.contains(name))
                    expectedTemperatures.insert(name);
            }
        }

        assert(lightComponents.size() == expectedLights.size());
        assert(temperatureComponents.size() == expectedTemperatures.size());

        for (const std::string& name : expectedLights) {
            assert(lightComponents.contains(name));
            assert(world.resolve_component(lightType, lightComponents.at(name)) != nullptr);
        }

        for (const std::string& name : expectedTemperatures) {
            assert(temperatureComponents.contains(name));
            assert(world.resolve_component(temperatureType, temperatureComponents.at(name)) != nullptr);
        }

        Signature signature = world.behavior_signature(behavior);
        bool hasLight = !expectedLights.empty();
        bool hasTemperature = !expectedTemperatures.empty();

        assert(signature.test(lightType.id) == hasLight);
        assert(signature.test(temperatureType.id) == hasTemperature);
        assert(world.system_has_behavior<StressLightSystem>(behavior) == hasLight);
        assert(world.system_has_behavior<StressTemperatureSystem>(behavior) == hasTemperature);
        assert(world.system_has_behavior<StressCombinedSystem>(behavior) == (hasLight && hasTemperature));
    }
}

void stress_world()
{
    std::mt19937 rng(0x123456u);
    World world;
    WorldModel model;

    ComponentType<StressLight> lightType = world.register_component<StressLight>("StressLight");
    ComponentType<StressTemperature> temperatureType = world.register_component<StressTemperature>("StressTemperature");
    std::vector<std::string> lightNames{"l0", "l1", "l2"};
    std::vector<std::string> temperatureNames{"t0", "t1", "t2"};

    for (const std::string& name : lightNames) {
        world.add_component(lightType, name, StressLight{1});
        model.liveLights.insert(name);
    }

    for (const std::string& name : temperatureNames) {
        world.add_component(temperatureType, name, StressTemperature{1});
        model.liveTemperatures.insert(name);
    }

    Signature lightSignature;
    lightSignature.set(lightType.id);
    Signature temperatureSignature;
    temperatureSignature.set(temperatureType.id);
    Signature combinedSignature = lightSignature | temperatureSignature;

    world.register_system<StressLightSystem>(lightSignature);
    world.register_system<StressTemperatureSystem>(temperatureSignature);
    world.register_system<StressCombinedSystem>(combinedSignature);

    std::vector<BehaviorId> activeBehaviors;

    for (int step = 0; step < 2500; ++step) {
        int operation = static_cast<int>(rng() % 12);

        if (operation < 2) {
            BehaviorId behavior = world.create_behavior();
            model.liveBehaviors.insert(behavior);
            activeBehaviors.push_back(behavior);
        } else if (operation < 4 && !activeBehaviors.empty()) {
            std::uniform_int_distribution<std::size_t> distribution(0, activeBehaviors.size() - 1);
            std::size_t index = distribution(rng);
            BehaviorId behavior = activeBehaviors[index];

            world.destroy_behavior(behavior);
            model.liveBehaviors.erase(behavior);
            model.lightAccess.erase(behavior);
            model.temperatureAccess.erase(behavior);
            activeBehaviors[index] = activeBehaviors.back();
            activeBehaviors.pop_back();
        } else if (operation < 7 && !activeBehaviors.empty()) {
            BehaviorId behavior = random_element(activeBehaviors, rng);
            const std::string& name = random_element(lightNames, rng);

            if (model.liveLights.contains(name)) {
                world.grant_component_access(lightType, behavior, name, ComponentAccessMode::ReadWrite);
                model.lightAccess[behavior].insert(name);
            }
        } else if (operation < 9 && !activeBehaviors.empty()) {
            BehaviorId behavior = random_element(activeBehaviors, rng);
            const std::string& name = random_element(temperatureNames, rng);

            if (model.liveTemperatures.contains(name)) {
                world.grant_component_access(temperatureType, behavior, name, ComponentAccessMode::Read);
                model.temperatureAccess[behavior].insert(name);
            }
        } else if (operation == 9 && !activeBehaviors.empty()) {
            BehaviorId behavior = random_element(activeBehaviors, rng);
            const std::string& name = random_element(lightNames, rng);

            if (model.liveLights.contains(name)) {
                world.revoke_component_access(lightType, behavior, name);
                model.lightAccess[behavior].erase(name);
            }
        } else if (operation == 10) {
            const std::string& name = random_element(lightNames, rng);

            if (model.liveLights.contains(name)) {
                world.remove_component(lightType, name);
                model.liveLights.erase(name);

                for (auto& [behavior, accesses] : model.lightAccess) {
                    (void)behavior;
                    accesses.erase(name);
                }
            } else {
                world.add_component(lightType, name, StressLight{step});
                model.liveLights.insert(name);
            }
        } else {
            const std::string& name = random_element(temperatureNames, rng);

            if (model.liveTemperatures.contains(name)) {
                world.remove_component(temperatureType, name);
                model.liveTemperatures.erase(name);

                for (auto& [behavior, accesses] : model.temperatureAccess) {
                    (void)behavior;
                    accesses.erase(name);
                }
            } else {
                world.add_component(temperatureType, name, StressTemperature{step});
                model.liveTemperatures.insert(name);
            }
        }

        assert(world.behavior_count() == model.liveBehaviors.size());
        assert_world_model(world, lightType, temperatureType, model);
    }
}

void stress_runtime_lua()
{
    using liquid::scripting::LuaBehaviorRunner;

    LuaBehaviorRunner runner;
    Runtime runtime;
    World& world = runtime.world();
    ComponentType<StressLight> lightType = world.register_component<StressLight>("StressLight");
    world.add_component(lightType, "light", StressLight{0});

    BehaviorId behavior = world.create_behavior();
    world.grant_component_access(lightType, behavior, "light", ComponentAccessMode::ReadWrite);
    ComponentSlotId slot = world.get_components(lightType, behavior).at("light");

    std::mt19937 rng(0x600Du);
    std::uniform_int_distribution<int> brightnessDistribution(0, 100);
    std::vector<int> brightnesses;
    brightnesses.reserve(1000);
    for (std::size_t index = 0; index < 1000; ++index)
        brightnesses.push_back(brightnessDistribution(rng));

    runner.expose_component(lightType, "StressLight", stress_light_codec());
    world.register_system<StressLuaSystem>(Signature{}, runner, behavior, brightnesses);

    std::size_t expectedLiveIntents = 0;
    bool temporaryIntentLive = false;

    for (FrameNumber frame = 0; frame < 1000; ++frame) {
        FrameLog log = runtime.run_frame(frame, {{lightType.id, {{"light", slot}}}});
        bool shouldFail = frame % 100 == 99;
        std::size_t expectedExpired = temporaryIntentLive ? 1 : 0;

        if (temporaryIntentLive) {
            expectedLiveIntents--;
            temporaryIntentLive = false;
        }

        if (frame == 0)
            expectedLiveIntents++;

        if (!shouldFail) {
            expectedLiveIntents++;
            temporaryIntentLive = true;
        }

        assert(log.completed);
        assert(log.frame == frame);
        assert(log.now == frame);
        assert(log.systems_run == 1);
        assert(log.resolution_requests == 1);
        assert(log.expired_intents == expectedExpired);
        assert(log.selected_intents == 1);
        assert(world.intent_count(behavior) == expectedLiveIntents);

        const auto& selections = log.intent_selections.at(lightType.id);
        IntentId selectedId = selections.at("light");
        const auto& selected = world.typed_intent(lightType, selectedId);
        assert(selected.owner == behavior);

        if (shouldFail) {
            assert(selected.value.value == brightnesses.front());
            assert(selected.priority == IntentPriority::Low);
            assert(selected.lifetime.kind == IntentLifetimeKind::Persistent);
        } else {
            assert(selected.value.value == brightnesses.at(static_cast<std::size_t>(frame)));
            assert(selected.priority == IntentPriority::Medium);
            assert(selected.lifetime.kind == IntentLifetimeKind::UntilTime);
            assert(selected.lifetime.expiresAt == frame + 1);
        }

        assert(world.get_component_named(lightType, "light")->value == 0);
        assert(!runtime.faulted());
        assert(runtime.frame() == frame + 1);
    }

    const auto& system = world.get_system<StressLuaSystem>();
    assert(system.successes == 990);
    assert(system.failures == 10);
}

int main()
{
    stress_behavior_registry();
    stress_intent_registry();
    stress_component_registry();
    stress_world();
    stress_runtime_lua();

    return 0;
}
