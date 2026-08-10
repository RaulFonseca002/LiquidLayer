#include "liquid/Runtime.hpp"
#include "liquid/scripting/LuaBehaviorRunner.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using liquid::scripting::LuaBehaviorRunner;
using liquid::scripting::LuaComponentCodec;
using liquid::scripting::LuaExecutionLimits;
using liquid::scripting::LuaExecutionResult;
using liquid::scripting::LuaExecutionStatus;
using liquid::scripting::LuaValue;

struct Light {
    int brightness = 0;
};

struct Sequence {
    std::vector<std::int64_t> values;
};

LuaComponentCodec<Light> light_codec() {
    return {
        [](const Light& light) {
            return LuaValue::Table{{"brightness", LuaValue{light.brightness}}};
        },
        [](const LuaValue& value) {
            const auto& table = value.as_table();

            if (table.size() != 1 || !table.contains("brightness"))
                throw std::runtime_error("Light requires exactly brightness");

            std::int64_t brightness = table.at("brightness").as_integer();

            if (brightness < 0 || brightness > 100)
                throw std::runtime_error("brightness must be between 0 and 100");

            return Light{static_cast<int>(brightness)};
        }
    };
}

LuaComponentCodec<Sequence> sequence_codec() {
    return {
        [](const Sequence& sequence) {
            LuaValue::Array values;
            for (std::int64_t value : sequence.values)
                values.emplace_back(value);
            return LuaValue(std::move(values));
        },
        [](const LuaValue& value) {
            Sequence sequence;
            for (const LuaValue& item : value.as_array())
                sequence.values.push_back(item.as_integer());
            return sequence;
        }
    };
}

void assert_status(const LuaExecutionResult& result, LuaExecutionStatus expected) {
    assert(result.status == expected);
    assert(result.succeeded() == (expected == LuaExecutionStatus::Success));
}

template <typename Function>
void expect_throw(Function function) {
    bool thrown = false;

    try {
        function();
    } catch (...) {
        thrown = true;
    }

    assert(thrown);
}

struct LuaOnceSystem : System {
    LuaBehaviorRunner* runner;
    BehaviorId owner;
    std::string source;
    bool ran = false;
    LuaExecutionResult result;

    LuaOnceSystem(LuaBehaviorRunner& behaviorRunner, BehaviorId behavior, std::string script)
        : runner(&behaviorRunner), owner(behavior), source(std::move(script))
    {
    }

    void run(World& world, FrameNumber frame, IntentTime now) override {
        (void)frame;

        if (ran)
            return;

        result = runner->execute(world, owner, now, source);
        ran = true;
    }
};

struct TrackingSystem : System {
    std::size_t runs = 0;

    void run(World& world, FrameNumber frame, IntentTime now) override {
        (void)world;
        (void)frame;
        (void)now;
        runs++;
    }
};

int main() {
    {
        World world;
        auto lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{10});
        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::ReadWrite);

        LuaBehaviorRunner runner;
        runner.expose_component(lightType, "Light", light_codec());

        LuaExecutionResult result = runner.execute(world, behavior, 100, R"(
            local light = access.Light.officeLight
            assert(light.value.brightness == 10)
            light.value.brightness = 100
            light.propose({
                value = light.value,
                priority = "high",
                duration_ms = 5
            })
            light.propose({
                value = { brightness = 30 },
                priority = "low",
                lifetime = "persistent"
            })
        )");

        assert_status(result, LuaExecutionStatus::Success);
        assert(result.createdIntents.size() == 2);
        assert(world.get_component_named(lightType, "officeLight")->brightness == 10);

        const auto& temporary = world.typed_intent(lightType, result.createdIntents[0]);
        const auto& fallback = world.typed_intent(lightType, result.createdIntents[1]);
        assert(temporary.value.brightness == 100);
        assert(temporary.priority == IntentPriority::High);
        assert(temporary.lifetime.kind == IntentLifetimeKind::UntilTime);
        assert(temporary.lifetime.expiresAt == 105);
        assert(fallback.value.brightness == 30);
        assert(fallback.priority == IntentPriority::Low);
        assert(fallback.lifetime.kind == IntentLifetimeKind::Persistent);
    }

    {
        World world;
        auto lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{10});
        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::Write);

        LuaExecutionLimits limits;
        limits.maxBufferedValueBytes = sizeof(LuaValue) * 3;
        LuaBehaviorRunner runner(limits);
        runner.expose_component(lightType, "Light", light_codec());
        LuaExecutionResult result = runner.execute(world, behavior, 0, R"(
            local propose = access.Light.officeLight.propose
            propose({ value = { brightness = 10 } })
            propose({ value = { brightness = 20 } })
        )");
        assert_status(result, LuaExecutionStatus::InvalidProposal);
        assert(world.intent_count(behavior) == 0);
    }

    {
        World world;
        auto lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "readOnly", Light{20});
        world.add_component(lightType, "writeOnly", Light{40});
        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "readOnly", ComponentAccessMode::Read);
        world.grant_component_access(lightType, behavior, "writeOnly", ComponentAccessMode::Write);

        LuaBehaviorRunner runner;
        runner.expose_component(lightType, "Light", light_codec());
        LuaExecutionResult result = runner.execute(world, behavior, 0, R"(
            assert(access.Light.readOnly.value.brightness == 20)
            assert(access.Light.readOnly.propose == nil)
            assert(access.Light.writeOnly.value == nil)
            assert(type(access.Light.writeOnly.propose) == "function")
        )");

        assert_status(result, LuaExecutionStatus::Success);
        assert(result.createdIntents.empty());
    }

    {
        World world;
        auto lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{10});
        world.add_component(lightType, "otherLight", Light{20});
        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::Write);

        LuaBehaviorRunner runner;
        runner.expose_component(lightType, "Light", light_codec());
        LuaExecutionResult result = runner.execute(world, behavior, 0, R"(
            access.Light.otherLight = {
                writable = true,
                owner = 999,
                slot = 999,
                propose = access.Light.officeLight.propose
            }
            access.Light.otherLight.propose({ value = { brightness = 55 } })
        )");

        assert_status(result, LuaExecutionStatus::Success);
        assert(result.createdIntents.size() == 1);
        ComponentTarget target = world.intent_target(result.createdIntents.front());
        assert(target.type == lightType.id);
        assert(world.intents_for(lightType.id, target.slot).size() == 1);
        assert(world.typed_intent(lightType, result.createdIntents.front()).value.brightness == 55);
    }

    {
        World world;
        auto lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{10});
        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::Write);

        LuaBehaviorRunner runner;
        runner.expose_component(lightType, "Light", light_codec());
        LuaExecutionResult result = runner.execute(world, behavior, 0, R"(
            access.Light.officeLight.propose({ value = { brightness = 75 } })
            error("do not commit")
        )");

        assert_status(result, LuaExecutionStatus::RuntimeError);
        assert(result.createdIntents.empty());
        assert(world.intent_count(behavior) == 0);
    }

    {
        World world;
        auto lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{10});
        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::ReadWrite);

        LuaBehaviorRunner runner;
        runner.expose_component(lightType, "Light", light_codec());
        assert_status(runner.execute(world, behavior, 0, "assert(access.Light.officeLight.propose ~= nil)"), LuaExecutionStatus::Success);

        world.revoke_component_access(lightType, behavior, "officeLight");
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::Read);
        assert_status(runner.execute(world, behavior, 0, "assert(access.Light.officeLight.propose == nil)"), LuaExecutionStatus::Success);

        world.remove_component(lightType, "officeLight");
        world.add_component(lightType, "replacement", Light{30});
        assert_status(runner.execute(world, behavior, 0, "assert(access.Light == nil or access.Light.replacement == nil)"), LuaExecutionStatus::Success);

        world.destroy_behavior(behavior);
        BehaviorId recycled = world.create_behavior();
        assert(recycled == behavior);
        assert_status(runner.execute(world, recycled, 0, "assert(access.Light == nil)"), LuaExecutionStatus::Success);
    }

    {
        World firstWorld;
        auto firstLightType = firstWorld.register_component<Light>("Light");
        firstWorld.add_component(firstLightType, "firstLight", Light{10});
        BehaviorId firstBehavior = firstWorld.create_behavior();
        firstWorld.grant_component_access(
            firstLightType,
            firstBehavior,
            "firstLight",
            ComponentAccessMode::Read
        );

        World secondWorld;
        auto secondLightType = secondWorld.register_component<Light>("Light");
        secondWorld.add_component(secondLightType, "secondLight", Light{20});
        BehaviorId secondBehavior = secondWorld.create_behavior();
        secondWorld.grant_component_access(
            secondLightType,
            secondBehavior,
            "secondLight",
            ComponentAccessMode::Read
        );

        assert(firstBehavior == secondBehavior);
        assert(firstWorld.behavior_access_revision(firstBehavior)
            == secondWorld.behavior_access_revision(secondBehavior));
        assert(firstWorld.instance_id() != secondWorld.instance_id());

        LuaBehaviorRunner runner;
        runner.expose_component(firstLightType, "Light", light_codec());
        assert_status(
            runner.execute(firstWorld, firstBehavior, 0, "assert(access.Light.firstLight ~= nil)"),
            LuaExecutionStatus::Success
        );
        assert_status(
            runner.execute(secondWorld, secondBehavior, 0, R"(
                assert(access.Light.firstLight == nil)
                assert(access.Light.secondLight.value.brightness == 20)
            )"),
            LuaExecutionStatus::Success
        );
    }

    {
        World world;
        BehaviorId behavior = world.create_behavior();
        LuaBehaviorRunner runner;
        LuaExecutionResult result = runner.execute(world, behavior, 0, R"(
            assert(_G == nil)
            assert(pcall == nil and xpcall == nil and tostring == nil)
            assert(os == nil and io == nil and package == nil and debug == nil and coroutine == nil)
            assert(load == nil and loadfile == nil and dofile == nil and require == nil)
            assert(collectgarbage == nil and getmetatable == nil and setmetatable == nil)
            assert(rawget == nil and rawset == nil)
            assert(string.dump == nil and string.find == nil and string.format == nil)
            assert(string.gmatch == nil and string.gsub == nil and string.match == nil)
            assert(("").match == nil)
            assert(math.random == nil and math.randomseed == nil)
        )");
        assert_status(result, LuaExecutionStatus::Success);
    }

    {
        World world;
        auto lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{10});
        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::Read);
        BehaviorAccessRevision revision = world.behavior_access_revision(behavior);

        LuaBehaviorRunner runner;
        runner.expose_component(lightType, "Light", light_codec());
        assert_status(runner.execute(world, behavior, 0, "assert(access.Light.officeLight.value.brightness == 10)"), LuaExecutionStatus::Success);

        world.get_component_named(lightType, "officeLight")->brightness = 65;
        assert(world.behavior_access_revision(behavior) == revision);
        assert_status(runner.execute(world, behavior, 0, "assert(access.Light.officeLight.value.brightness == 65)"), LuaExecutionStatus::Success);

        expect_throw([&] {
            runner.expose_component(lightType, "AnotherLight", light_codec());
        });
    }

    {
        World world;
        auto lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{10});
        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::Write);

        LuaBehaviorRunner runner;
        runner.expose_component(lightType, "Light", light_codec());

        for (const char* source : {
            "access.Light.officeLight.propose({ value = { brightness = 10 }, priority = 'urgent' })",
            "access.Light.officeLight.propose({ value = { brightness = 10 }, lifetime = 'frame' })",
            "access.Light.officeLight.propose({ value = { brightness = 10 }, lifetime = 'persistent', duration_ms = 1 })",
            "access.Light.officeLight.propose({ value = { brightness = 10 }, unexpected = true })",
            "local value = {}; value.self = value; access.Light.officeLight.propose({ value = value })"
        }) {
            LuaExecutionResult result = runner.execute(world, behavior, 0, source);
            assert_status(result, LuaExecutionStatus::InvalidProposal);
            assert(world.intent_count(behavior) == 0);
        }

        LuaExecutionResult overflow = runner.execute(
            world,
            behavior,
            static_cast<IntentTime>(std::numeric_limits<std::int64_t>::max()) - 1,
            "access.Light.officeLight.propose({ value = { brightness = 10 }, duration_ms = 2 })"
        );
        assert_status(overflow, LuaExecutionStatus::InvalidProposal);
        assert(world.intent_count(behavior) == 0);

        LuaExecutionResult unrepresentableTime = runner.execute(
            world,
            behavior,
            static_cast<IntentTime>(std::numeric_limits<std::int64_t>::max()) + 1,
            "return"
        );
        assert_status(unrepresentableTime, LuaExecutionStatus::HostError);
    }

    {
        World world;
        auto sequenceType = world.register_component<Sequence>("Sequence");
        world.add_component(sequenceType, "items", Sequence{});
        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(sequenceType, behavior, "items", ComponentAccessMode::ReadWrite);

        LuaBehaviorRunner runner;
        runner.expose_component(sequenceType, "Sequence", sequence_codec());
        LuaExecutionResult result = runner.execute(world, behavior, 0, R"(
            assert(#access.Sequence.items.value == 0)
            access.Sequence.items.propose({ value = access.Sequence.items.value })
        )");

        assert_status(result, LuaExecutionStatus::Success);
        assert(result.createdIntents.size() == 1);
        assert(world.typed_intent(sequenceType, result.createdIntents.front()).value.values.empty());
    }

    {
        World world;
        BehaviorId behavior = world.create_behavior();
        LuaBehaviorRunner runner;

        assert_status(runner.execute(world, behavior, 0, "temporary_global = 42"), LuaExecutionStatus::Success);
        assert_status(runner.execute(world, behavior, 0, "assert(temporary_global == nil)"), LuaExecutionStatus::Success);
        assert_status(runner.execute(world, behavior + 1, 0, "return"), LuaExecutionStatus::InvalidBehavior);

        std::string binaryChunk("\x1bLua", 4);
        assert_status(runner.execute(world, behavior, 0, binaryChunk), LuaExecutionStatus::SyntaxError);

        std::string embeddedNul("return\0true", 11);
        assert_status(runner.execute(world, behavior, 0, embeddedNul), LuaExecutionStatus::SyntaxError);
    }

    {
        World world;
        BehaviorId behavior = world.create_behavior();
        LuaExecutionLimits limits;
        limits.maxSourceBytes = 8;
        limits.maxDiagnosticBytes = 32;
        LuaBehaviorRunner runner(limits);

        assert_status(runner.execute(world, behavior, 0, "return 1"), LuaExecutionStatus::Success);
        assert_status(runner.execute(world, behavior, 0, "123456789"), LuaExecutionStatus::SourceLimitExceeded);

        limits.maxSourceBytes = 1024;
        LuaBehaviorRunner diagnosticRunner(limits);
        LuaExecutionResult diagnostic = diagnosticRunner.execute(world, behavior, 0, "error('this diagnostic is intentionally much longer than thirty two bytes')");
        assert_status(diagnostic, LuaExecutionStatus::RuntimeError);
        assert(diagnostic.diagnostic.size() <= limits.maxDiagnosticBytes);

        LuaExecutionResult nonString = diagnosticRunner.execute(world, behavior, 0, "error({})");
        assert_status(nonString, LuaExecutionStatus::RuntimeError);
        assert(nonString.diagnostic == "Lua returned a non-string error");
    }

    {
        World world;
        BehaviorId behavior = world.create_behavior();
        LuaExecutionLimits limits;
        limits.maxMemoryBytes = 1;
        LuaBehaviorRunner runner(limits);
        assert_status(runner.execute(world, behavior, 0, "return"), LuaExecutionStatus::MemoryLimitExceeded);
    }

    {
        World world;
        BehaviorId behavior = world.create_behavior();
        LuaExecutionLimits limits;
        limits.maxMemoryBytes = 256 * 1024;
        LuaBehaviorRunner runner(limits);
        LuaExecutionResult result = runner.execute(world, behavior, 0, R"(
            local values = {}
            while true do
                values[#values + 1] = string.rep("x", 4096)
            end
        )");
        assert_status(result, LuaExecutionStatus::MemoryLimitExceeded);
    }

    {
        World world;
        BehaviorId behavior = world.create_behavior();
        LuaExecutionLimits limits;
        limits.maxInstructions = 1'000;
        LuaBehaviorRunner runner(limits);
        LuaExecutionResult result = runner.execute(world, behavior, 0, "while true do end");
        assert_status(result, LuaExecutionStatus::InstructionLimitExceeded);
        assert(result.diagnostic.size() <= limits.maxDiagnosticBytes);
    }

    {
        World world;
        auto lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{10});
        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::Write);

        LuaExecutionLimits limits;
        limits.maxTableDepth = 2;
        limits.maxTableEntries = 2;
        limits.maxStringBytes = 8;
        limits.maxBufferedValueBytes = 256;
        LuaBehaviorRunner runner(limits);
        runner.expose_component(lightType, "Light", light_codec());

        for (const char* source : {
            "access.Light.officeLight.propose({ value = { brightness = 0/0 } })",
            "access.Light.officeLight.propose({ value = { brightness = math.huge } })",
            "access.Light.officeLight.propose({ value = { nested = { too = { still = { deep = 1 } } } } })",
            "access.Light.officeLight.propose({ value = { a = 1, b = 2, c = 3 } })",
            "access.Light.officeLight.propose({ value = string.rep('x', 9) })"
        }) {
            assert_status(runner.execute(world, behavior, 0, source), LuaExecutionStatus::InvalidProposal);
            assert(world.intent_count(behavior) == 0);
        }
    }

    {
        World world;
        auto lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{10});
        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::Write);

        LuaExecutionLimits limits;
        limits.maxCreatedIntents = 1;
        LuaBehaviorRunner runner(limits);
        runner.expose_component(lightType, "Light", light_codec());
        LuaExecutionResult result = runner.execute(world, behavior, 0, R"(
            local propose = access.Light.officeLight.propose
            propose({ value = { brightness = 10 } })
            propose({ value = { brightness = 20 } })
        )");
        assert_status(result, LuaExecutionStatus::IntentLimitExceeded);
        assert(world.intent_count(behavior) == 0);
    }

    {
        World world;
        auto lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{10});
        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::Write);
        ComponentSlotId slot = world.get_components(lightType, behavior).at("officeLight");

        for (std::size_t i = 0; i + 1 < MAX_INTENTS; ++i) {
            world.create_intent(
                behavior,
                lightType,
                slot,
                IntentLifetime::persistent(),
                Light{static_cast<int>(i % 101)},
                IntentPriority::Low
            );
        }

        assert(world.intent_count(behavior) == MAX_INTENTS - 1);
        LuaBehaviorRunner runner;
        runner.expose_component(lightType, "Light", light_codec());
        LuaExecutionResult result = runner.execute(world, behavior, 0, R"(
            local propose = access.Light.officeLight.propose
            propose({ value = { brightness = 90 } })
            propose({ value = { brightness = 80 } })
        )");

        assert_status(result, LuaExecutionStatus::CommitFailed);
        assert(result.createdIntents.empty());
        assert(world.intent_count(behavior) == MAX_INTENTS - 1);
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        auto lightType = world.register_component<Light>("Light");
        world.add_component(lightType, "officeLight", Light{10});
        BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "officeLight", ComponentAccessMode::Write);
        ComponentSlotId slot = world.get_components(lightType, behavior).at("officeLight");

        LuaBehaviorRunner runner;
        runner.expose_component(lightType, "Light", light_codec());
        world.register_system<LuaOnceSystem>(Signature{}, runner, behavior, R"(
            local propose = access.Light.officeLight.propose
            propose({ value = { brightness = 100 }, priority = "high", duration_ms = 5 })
            propose({ value = { brightness = 30 }, priority = "low" })
        )");
        world.register_system<TrackingSystem>(Signature{});

        FrameLog first = runtime.run_frame(100, {{lightType.id, {{"officeLight", slot}}}});
        auto& luaSystem = world.get_system<LuaOnceSystem>();
        assert(first.completed);
        assert(!runtime.faulted());
        assert_status(luaSystem.result, LuaExecutionStatus::Success);
        assert(first.intent_selections.at(lightType.id).at("officeLight") == luaSystem.result.createdIntents[0]);
        assert(world.get_system<TrackingSystem>().runs == 1);

        FrameLog second = runtime.run_frame(105, {{lightType.id, {{"officeLight", slot}}}});
        assert(second.completed);
        assert(second.intent_selections.at(lightType.id).at("officeLight") == luaSystem.result.createdIntents[1]);
        assert(world.get_system<TrackingSystem>().runs == 2);
    }

    {
        Runtime runtime;
        World& world = runtime.world();
        BehaviorId behavior = world.create_behavior();
        LuaBehaviorRunner runner;
        world.register_system<LuaOnceSystem>(Signature{}, runner, behavior, "error('script failure')");
        world.register_system<TrackingSystem>(Signature{});

        FrameLog log = runtime.run_frame(0);
        assert(log.completed);
        assert(!runtime.faulted());
        assert_status(world.get_system<LuaOnceSystem>().result, LuaExecutionStatus::RuntimeError);
        assert(world.get_system<TrackingSystem>().runs == 1);
    }

    return 0;
}
