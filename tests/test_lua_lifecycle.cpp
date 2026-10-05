#include "liquid/Runtime.hpp"
#include "liquid/scripting/LuaLifecycleSystem.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace liquid;
using namespace liquid::scripting;

namespace {

struct Light {
    std::int64_t level = 0;
};

ComponentCodec<Light> light_codec() {
    return {
        [](const Light& light) { return Value{light.level}; },
        [](const Value& value) { return Light{value.as_signed_integer()}; }
    };
}

LuaComponentCodec<Light> lua_light_codec() {
    return {
        [](const Light& light) {
            return LuaValue{LuaValue::Table{{"level", LuaValue{light.level}}}};
        },
        [](const LuaValue& value) {
            return Light{value.as_table().at("level").as_integer()};
        }
    };
}

Signature script_signature(ComponentType<LuaBehaviorScript> scriptType) {
    Signature signature;
    signature.set(scriptType.id);
    return signature;
}

}

TEST_CASE("Lua lifecycle callbacks drive the same-frame Solid loop") {
    RuntimeOptions options;
    options.sessionId = SessionId{1};
    options.allowVolatileEffects = true;
    Runtime runtime(options);
    World& world = runtime.world();
    const auto lightType = world.register_component<Light>(
        "example.Light", 1, light_codec());
    const auto scriptType = world.register_component<LuaBehaviorScript>(
        "liquid.LuaBehaviorScript", 1, lua_behavior_script_codec());

    const std::string source = R"lua(
function on_start(frame)
    assert(frame.number == 0)
    solid.watch(access.Light.office)
end

function on_components_changed(frame, changes)
    assert(frame.number == 1)
    assert(#changes == 1)
    assert(changes[1].before.level == 10)
    assert(changes[1].after.level == 70)
    solid.cancel(solid.owned_intents.raise_light)
    access.Light.office.propose{
        name = "lower_light",
        value = {level = 30},
        priority = "high"
    }
end

function on_frame(frame)
    if frame.number == 0 then
        access.Light.office.propose{
            name = "raise_light",
            value = {level = 70},
            priority = "high"
        }
    end
end
)lua";

    world.add_component(lightType, "office", Light{10});
    world.add_component(
        scriptType, "lifecycle", LuaBehaviorScript{source, 1});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(
        lightType, behavior, "office", ComponentAccessMode::ReadWrite);
    world.grant_component_access(
        scriptType, behavior, "lifecycle", ComponentAccessMode::Read);

    auto runner = std::make_shared<LuaBehaviorRunner>();
    runner->expose_component(lightType, "Light", lua_light_codec());
    world.register_system<LuaLifecycleSystem>(
        script_signature(scriptType),
        SystemPhase::Behavior,
        scriptType,
        runner);
    runtime.configure_component(lightType, "office", ComponentControl::InternalState);

    FrameInput firstInput;
    firstInput.now = 100;
    const FrameLog first = runtime.run_frame(std::move(firstInput)).frame;
    REQUIRE(first.completed);
    const auto& firstSystem = world.get_system<LuaLifecycleSystem>();
    REQUIRE(firstSystem.last_result(behavior) != nullptr);
    INFO(firstSystem.last_result(behavior)->diagnostic);
    REQUIRE(firstSystem.last_result(behavior)->succeeded());
    REQUIRE(world.read_component(lightType, behavior, "office")->level == 70);
    const std::optional<IntentId> raised = world.intent_named(behavior, "raise_light");
    REQUIRE(raised.has_value());

    FrameInput secondInput;
    secondInput.now = 105;
    const FrameLog second = runtime.run_frame(std::move(secondInput)).frame;
    REQUIRE(second.completed);
    REQUIRE(world.read_component(lightType, behavior, "office")->level == 30);
    REQUIRE(!world.intent_exists(*raised));
    REQUIRE(!world.intent_named(behavior, "raise_light").has_value());
    REQUIRE(world.intent_named(behavior, "lower_light").has_value());

    const auto& system = world.get_system<LuaLifecycleSystem>();
    REQUIRE(system.last_result(behavior) != nullptr);
    REQUIRE(system.last_result(behavior)->succeeded());
    REQUIRE(system.last_result(behavior)->cancelledIntents ==
        std::vector<IntentId>{*raised});
}

TEST_CASE("failed lifecycle bundles commit no proposals or watches") {
    RuntimeOptions options;
    options.sessionId = SessionId{2};
    options.allowVolatileEffects = true;
    Runtime runtime(options);
    World& world = runtime.world();
    const auto lightType = world.register_component<Light>(
        "example.Light", 1, light_codec());
    const auto scriptType = world.register_component<LuaBehaviorScript>(
        "liquid.LuaBehaviorScript", 1, lua_behavior_script_codec());

    world.add_component(lightType, "office", Light{10});
    world.add_component(scriptType, "lifecycle", LuaBehaviorScript{R"lua(
function on_start(frame)
    solid.watch(access.Light.office)
end
function on_frame(frame)
    access.Light.office.propose{name = "never_committed", value = {level = 70}}
    error("callback failed")
end
)lua", 1});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(
        lightType, behavior, "office", ComponentAccessMode::ReadWrite);
    world.grant_component_access(
        scriptType, behavior, "lifecycle", ComponentAccessMode::Read);

    auto runner = std::make_shared<LuaBehaviorRunner>();
    runner->expose_component(lightType, "Light", lua_light_codec());
    world.register_system<LuaLifecycleSystem>(
        script_signature(scriptType),
        SystemPhase::Behavior,
        scriptType,
        runner);
    runtime.configure_component(lightType, "office", ComponentControl::InternalState);

    FrameInput input;
    input.now = 100;
    const FrameLog frame = runtime.run_frame(std::move(input)).frame;
    REQUIRE(frame.completed);
    REQUIRE(world.intent_count(behavior) == 0);
    REQUIRE(world.read_component(lightType, behavior, "office")->level == 10);

    const auto& system = world.get_system<LuaLifecycleSystem>();
    REQUIRE(system.last_result(behavior) != nullptr);
    REQUIRE(!system.last_result(behavior)->succeeded());
    REQUIRE(system.last_result(behavior)->watches.empty());
}

namespace {

std::vector<LuaBehaviorRunner::ComponentChange> large_changes(std::size_t count) {
    std::vector<LuaBehaviorRunner::ComponentChange> changes;
    changes.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        changes.push_back({
            "Light",
            "office" + std::to_string(index),
            LuaValue{std::string(4096, 'a')},
            LuaValue{std::string(4096, 'b')}
        });
    }
    return changes;
}

}

// On the reviewed code these cases abort the whole test executable with
// SIGABRT because lifecycle argument construction ran outside any protected
// Lua call. ctest reports that abort as a failed test.
TEST_CASE("lifecycle change arguments exhausting Lua memory yield a bounded result") {
    const std::string source = R"lua(
function on_components_changed(frame, changes) end
function on_frame(frame) end
)lua";
    const auto changes = large_changes(16);

    for (const std::size_t limit : {16384u, 24576u, 32768u, 65536u, 131072u}) {
        INFO("maxMemoryBytes = " << limit);
        World world;
        const BehaviorId behavior = world.create_behavior();
        LuaExecutionLimits limits;
        limits.maxMemoryBytes = limit;
        LuaBehaviorRunner runner(limits);

        const LuaExecutionResult result = runner.execute_lifecycle(
            world, behavior, 0, 100, 0, false, changes, source);
        REQUIRE(result.status == LuaExecutionStatus::MemoryLimitExceeded);
        REQUIRE(result.createdIntents.empty());
        REQUIRE(result.cancelledIntents.empty());
        REQUIRE(result.watches.empty());
        REQUIRE(world.intent_count(behavior) == 0);

        LuaBehaviorRunner healthy;
        const LuaExecutionResult next = healthy.execute_lifecycle(
            world, behavior, 1, 200, 100, false, changes, source);
        REQUIRE(next.succeeded());
    }
}

TEST_CASE("lifecycle argument exhaustion after on_start commits nothing") {
    World world;
    const auto lightType = world.register_component<Light>(
        "example.Light", 1, light_codec());
    world.add_component(lightType, "office", Light{10});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(
        lightType, behavior, "office", ComponentAccessMode::ReadWrite);

    LuaExecutionLimits limits;
    limits.maxMemoryBytes = 65536;
    LuaBehaviorRunner runner(limits);
    runner.expose_component(lightType, "Light", lua_light_codec());

    const std::string source = R"lua(
function on_start(frame)
    solid.watch(access.Light.office)
    access.Light.office.propose{name = "queued", value = {level = 70}}
end
function on_components_changed(frame, changes) end
function on_frame(frame) end
)lua";
    const auto changes = large_changes(16);
    const LuaExecutionResult result = runner.execute_lifecycle(
        world, behavior, 0, 100, 0, true, changes, source);
    REQUIRE(result.status == LuaExecutionStatus::MemoryLimitExceeded);
    REQUIRE(result.createdIntents.empty());
    REQUIRE(result.watches.empty());
    REQUIRE(world.intent_count(behavior) == 0);
    REQUIRE(world.read_component(lightType, behavior, "office")->level == 10);

    const LuaExecutionResult next = runner.execute_lifecycle(
        world, behavior, 1, 200, 100, true, {}, source);
    REQUIRE(next.succeeded());
    REQUIRE(world.intent_count(behavior) == 1);
}

TEST_CASE("frame argument construction never escapes the Lua memory bound") {
    const std::string source = R"lua(
function on_start(frame) end
function on_frame(frame) end
)lua";
    bool sawBoundedFailure = false;
    bool sawSuccess = false;
    for (std::size_t limit = 4096; limit <= 65536; limit += 256) {
        INFO("maxMemoryBytes = " << limit);
        World world;
        const BehaviorId behavior = world.create_behavior();
        LuaExecutionLimits limits;
        limits.maxMemoryBytes = limit;
        LuaBehaviorRunner runner(limits);
        const LuaExecutionResult result = runner.execute_lifecycle(
            world, behavior, 0, 100, 0, true, {}, source);
        REQUIRE((result.status == LuaExecutionStatus::Success ||
                 result.status == LuaExecutionStatus::MemoryLimitExceeded));
        sawBoundedFailure = sawBoundedFailure ||
            result.status == LuaExecutionStatus::MemoryLimitExceeded;
        sawSuccess = sawSuccess || result.succeeded();
    }
    REQUIRE(sawBoundedFailure);
    REQUIRE(sawSuccess);
}

namespace {

constexpr const char* NoReadableScript =
    "lifecycle script selection: no readable LuaBehaviorScript component";
constexpr const char* MultipleReadableScripts =
    "lifecycle script selection: multiple readable LuaBehaviorScript components";
constexpr const char* UnavailableScript =
    "lifecycle script selection: script component is unavailable";

RuntimeOptions selection_options(std::uint64_t session) {
    RuntimeOptions options;
    options.sessionId = SessionId{session};
    options.allowVolatileEffects = true;
    return options;
}

// on_start is the only callback that watches, so a single watch in a result
// marks the frame on which on_start ran.
std::string watching_source(const std::string& light, const std::string& suffix = "") {
    return "function on_start(frame)\n"
           "    solid.watch(access.Light." + light + ")\n"
           "end\n"
           "function on_frame(frame) end\n" + suffix;
}

std::string proposing_source(
    const std::string& light,
    const std::string& intent,
    std::int64_t level
) {
    return "function on_start(frame)\n"
           "    access.Light." + light + ".propose{name = \"" + intent +
           "\", value = {level = " + std::to_string(level) + "}}\n"
           "end\n"
           "function on_frame(frame) end\n";
}

bool ran_on_start(const LuaExecutionResult* result) {
    return result != nullptr && result->succeeded() && result->watches.size() == 1;
}

bool ran_without_start(const LuaExecutionResult* result) {
    return result != nullptr && result->succeeded() && result->watches.empty();
}

bool is_selection_failure(const LuaExecutionResult* result, const std::string& diagnostic) {
    return result != nullptr &&
        result->status == LuaExecutionStatus::HostError &&
        result->diagnostic == diagnostic &&
        result->createdIntents.empty() &&
        result->cancelledIntents.empty() &&
        result->watches.empty();
}

struct SelectionWorld {
    Runtime runtime;
    World& world;
    ComponentType<Light> lightType;
    ComponentType<LuaBehaviorScript> scriptType;
    std::shared_ptr<LuaBehaviorRunner> runner;

    explicit SelectionWorld(std::uint64_t session)
        : runtime(selection_options(session)),
          world(runtime.world()),
          lightType(world.register_component<Light>("example.Light", 1, light_codec())),
          scriptType(world.register_component<LuaBehaviorScript>(
              "liquid.LuaBehaviorScript", 1, lua_behavior_script_codec())),
          runner(std::make_shared<LuaBehaviorRunner>())
    {
        runner->expose_component(lightType, "Light", lua_light_codec());
    }

    void add_light(const std::string& name) {
        world.add_component(lightType, name, Light{10});
        runtime.configure_component(lightType, name, ComponentControl::InternalState);
    }

    BehaviorId behavior_on(const std::string& light) {
        const BehaviorId behavior = world.create_behavior();
        world.grant_component_access(
            lightType, behavior, light, ComponentAccessMode::ReadWrite);
        return behavior;
    }

    void add_script(const std::string& name, std::string source, std::uint32_t revision = 1) {
        world.add_component(scriptType, name, LuaBehaviorScript{std::move(source), revision});
    }

    void grant_script(
        BehaviorId behavior,
        const std::string& name,
        ComponentAccessMode mode = ComponentAccessMode::Read
    ) {
        world.grant_component_access(scriptType, behavior, name, mode);
    }

    void use_single_readable() {
        world.register_system<LuaLifecycleSystem>(
            script_signature(scriptType),
            SystemPhase::Behavior,
            scriptType,
            runner,
            LuaScriptSelection::SingleReadable);
    }

    bool frame(IntentTime now) {
        FrameInput input;
        input.now = now;
        const FrameLog log = runtime.run_frame(std::move(input)).frame;
        return log.completed && !runtime.faulted();
    }

    const LuaExecutionResult* result(BehaviorId behavior) const {
        return world.get_system<LuaLifecycleSystem>().last_result(behavior);
    }

    std::int64_t level(BehaviorId behavior, const std::string& light) const {
        return world.read_component(lightType, behavior, light)->level;
    }
};

}

TEST_CASE("single-readable selection runs behaviors with different script slots independently",
          "[l2.1]") {
    SelectionWorld fixture(101);
    fixture.add_light("office");
    fixture.add_light("hall");
    fixture.add_script("script_a", proposing_source("office", "office_level", 11));
    fixture.add_script("script_b", proposing_source("hall", "hall_level", 22));
    const BehaviorId first = fixture.behavior_on("office");
    const BehaviorId second = fixture.behavior_on("hall");
    fixture.grant_script(first, "script_a");
    fixture.grant_script(second, "script_b");
    fixture.use_single_readable();

    REQUIRE(fixture.frame(100));
    REQUIRE(fixture.result(first) != nullptr);
    INFO(fixture.result(first)->diagnostic);
    REQUIRE(fixture.result(first)->succeeded());
    REQUIRE(fixture.result(second) != nullptr);
    INFO(fixture.result(second)->diagnostic);
    REQUIRE(fixture.result(second)->succeeded());
    REQUIRE(fixture.level(first, "office") == 11);
    REQUIRE(fixture.level(second, "hall") == 22);
    REQUIRE(fixture.world.intent_named(first, "office_level").has_value());
    REQUIRE(fixture.world.intent_named(second, "hall_level").has_value());
    REQUIRE(fixture.world.intent_count(first) == 1);
    REQUIRE(fixture.world.intent_count(second) == 1);
}

TEST_CASE("single-readable selection without a readable script fails only that behavior",
          "[l2.1]") {
    SelectionWorld fixture(102);
    fixture.add_light("office");
    fixture.add_light("hall");
    fixture.add_script("lifecycle", proposing_source("hall", "hall_level", 22));
    const BehaviorId unscripted = fixture.behavior_on("office");
    const BehaviorId scripted = fixture.behavior_on("hall");
    fixture.grant_script(scripted, "lifecycle");

    // Membership by light access makes a behavior with no script slot a member.
    Signature lights;
    lights.set(fixture.lightType.id);
    fixture.world.register_system<LuaLifecycleSystem>(
        lights,
        SystemPhase::Behavior,
        fixture.scriptType,
        fixture.runner,
        LuaScriptSelection::SingleReadable);

    REQUIRE(fixture.frame(100));
    REQUIRE(is_selection_failure(fixture.result(unscripted), NoReadableScript));
    REQUIRE(fixture.world.intent_count(unscripted) == 0);
    REQUIRE(fixture.level(unscripted, "office") == 10);

    REQUIRE(fixture.result(scripted) != nullptr);
    REQUIRE(fixture.result(scripted)->succeeded());
    REQUIRE(fixture.level(scripted, "hall") == 22);
}

TEST_CASE("single-readable selection with several readable scripts runs no source",
          "[l2.1]") {
    SelectionWorld fixture(103);
    fixture.add_light("office");
    fixture.add_script("lifecycle", proposing_source("office", "first_level", 30));
    fixture.add_script("other", proposing_source("office", "second_level", 40));
    const BehaviorId behavior = fixture.behavior_on("office");
    fixture.grant_script(behavior, "lifecycle");
    fixture.grant_script(behavior, "other", ComponentAccessMode::ReadWrite);
    fixture.use_single_readable();

    REQUIRE(fixture.frame(100));
    REQUIRE(is_selection_failure(fixture.result(behavior), MultipleReadableScripts));
    REQUIRE(fixture.world.intent_count(behavior) == 0);
    REQUIRE(fixture.level(behavior, "office") == 10);
}

TEST_CASE("write-only script slots are not readable for single-readable selection",
          "[l2.1]") {
    SelectionWorld fixture(104);
    fixture.add_light("office");
    fixture.add_light("hall");
    fixture.add_script("lifecycle", proposing_source("office", "write_only_level", 50));
    fixture.add_script("main", proposing_source("hall", "hall_level", 22));

    const BehaviorId writeOnly = fixture.behavior_on("office");
    fixture.grant_script(writeOnly, "lifecycle", ComponentAccessMode::Write);

    const BehaviorId mixed = fixture.behavior_on("hall");
    fixture.grant_script(mixed, "lifecycle", ComponentAccessMode::Write);
    fixture.grant_script(mixed, "main");
    fixture.use_single_readable();

    REQUIRE(fixture.frame(100));
    REQUIRE(is_selection_failure(fixture.result(writeOnly), NoReadableScript));
    REQUIRE(fixture.world.intent_count(writeOnly) == 0);
    REQUIRE(fixture.level(writeOnly, "office") == 10);

    REQUIRE(fixture.result(mixed) != nullptr);
    INFO(fixture.result(mixed)->diagnostic);
    REQUIRE(fixture.result(mixed)->succeeded());
    REQUIRE(fixture.level(mixed, "hall") == 22);
    REQUIRE(!fixture.world.intent_named(mixed, "write_only_level").has_value());
}

TEST_CASE("selecting a differently named script slot restarts the lifecycle", "[l2.1]") {
    SelectionWorld fixture(105);
    fixture.add_light("office");
    const std::string source = watching_source("office");
    fixture.add_script("lifecycle", source);
    fixture.add_script("renamed", source);
    const BehaviorId behavior = fixture.behavior_on("office");
    fixture.grant_script(behavior, "lifecycle");
    fixture.use_single_readable();

    REQUIRE(fixture.frame(100));
    REQUIRE(ran_on_start(fixture.result(behavior)));
    REQUIRE(fixture.frame(110));
    REQUIRE(ran_without_start(fixture.result(behavior)));

    // Same source and revision under another name: only the slot changes.
    fixture.grant_script(behavior, "renamed");
    fixture.world.revoke_component_access(fixture.scriptType, behavior, "lifecycle");
    REQUIRE(fixture.frame(120));
    REQUIRE(ran_on_start(fixture.result(behavior)));
    REQUIRE(fixture.frame(130));
    REQUIRE(ran_without_start(fixture.result(behavior)));
}

TEST_CASE("a re-created script slot with the same name restarts the lifecycle", "[l2.1]") {
    SelectionWorld fixture(106);
    fixture.add_light("office");
    const std::string source = watching_source("office");
    fixture.add_script("lifecycle", source);
    fixture.add_script("spare", watching_source("office", "-- spare\n"));
    const BehaviorId behavior = fixture.behavior_on("office");
    fixture.grant_script(behavior, "lifecycle");
    // The write-only slot keeps the behavior a system member while the
    // selected slot is removed; it is never a selection candidate.
    fixture.grant_script(behavior, "spare", ComponentAccessMode::Write);
    fixture.use_single_readable();

    REQUIRE(fixture.frame(100));
    REQUIRE(ran_on_start(fixture.result(behavior)));
    REQUIRE(fixture.frame(110));
    REQUIRE(ran_without_start(fixture.result(behavior)));

    const ComponentSlotId previous =
        fixture.world.get_components(fixture.scriptType, behavior).at("lifecycle");
    fixture.world.remove_component(fixture.scriptType, "lifecycle");
    fixture.add_script("lifecycle", source);
    fixture.grant_script(behavior, "lifecycle");
    REQUIRE(fixture.world.system_has_behavior<LuaLifecycleSystem>(behavior));
    REQUIRE(fixture.world.get_components(fixture.scriptType, behavior).at("lifecycle") !=
        previous);

    REQUIRE(fixture.frame(120));
    REQUIRE(ran_on_start(fixture.result(behavior)));
    REQUIRE(fixture.frame(130));
    REQUIRE(ran_without_start(fixture.result(behavior)));
}

TEST_CASE("an edited script source or revision restarts the lifecycle", "[l2.1]") {
    SelectionWorld fixture(107);
    fixture.add_light("office");
    const std::string source = watching_source("office");
    fixture.add_script("main", source);
    const BehaviorId behavior = fixture.behavior_on("office");
    fixture.grant_script(behavior, "main", ComponentAccessMode::ReadWrite);
    fixture.use_single_readable();

    REQUIRE(fixture.frame(100));
    REQUIRE(ran_on_start(fixture.result(behavior)));
    REQUIRE(fixture.frame(110));
    REQUIRE(ran_without_start(fixture.result(behavior)));

    const std::string edited = watching_source("office", "-- edited\n");
    fixture.world.replace_component(
        fixture.scriptType, behavior, "main", LuaBehaviorScript{edited, 1});
    REQUIRE(fixture.frame(120));
    REQUIRE(ran_on_start(fixture.result(behavior)));
    REQUIRE(fixture.frame(130));
    REQUIRE(ran_without_start(fixture.result(behavior)));

    fixture.world.replace_component(
        fixture.scriptType, behavior, "main", LuaBehaviorScript{edited, 2});
    REQUIRE(fixture.frame(140));
    REQUIRE(ran_on_start(fixture.result(behavior)));
    REQUIRE(fixture.frame(150));
    REQUIRE(ran_without_start(fixture.result(behavior)));
}

TEST_CASE("a script selection failure followed by recovery runs on_start again", "[l2.1]") {
    SelectionWorld fixture(108);
    fixture.add_light("office");
    fixture.add_script("lifecycle", watching_source("office"));
    fixture.add_script("second", watching_source("office", "-- second\n"));
    const BehaviorId behavior = fixture.behavior_on("office");
    fixture.grant_script(behavior, "lifecycle");
    fixture.use_single_readable();

    REQUIRE(fixture.frame(100));
    REQUIRE(ran_on_start(fixture.result(behavior)));
    REQUIRE(fixture.frame(110));
    REQUIRE(ran_without_start(fixture.result(behavior)));

    fixture.grant_script(behavior, "second");
    REQUIRE(fixture.frame(120));
    REQUIRE(is_selection_failure(fixture.result(behavior), MultipleReadableScripts));

    fixture.world.revoke_component_access(fixture.scriptType, behavior, "second");
    REQUIRE(fixture.frame(130));
    REQUIRE(ran_on_start(fixture.result(behavior)));
    REQUIRE(fixture.frame(140));
    REQUIRE(ran_without_start(fixture.result(behavior)));
}

TEST_CASE("removing a behavior clears its single-readable lifecycle cache", "[l2.1]") {
    SelectionWorld fixture(109);
    fixture.add_light("office");
    fixture.add_light("hall");
    fixture.add_script("main", watching_source("office"));
    fixture.add_script("hall_script", watching_source("hall"));
    const BehaviorId removed = fixture.behavior_on("office");
    const BehaviorId revoked = fixture.behavior_on("hall");
    fixture.grant_script(removed, "main");
    fixture.grant_script(revoked, "hall_script");
    fixture.use_single_readable();

    REQUIRE(fixture.frame(100));
    REQUIRE(ran_on_start(fixture.result(removed)));
    REQUIRE(ran_on_start(fixture.result(revoked)));

    fixture.world.destroy_behavior(removed);
    fixture.world.revoke_component_access(fixture.scriptType, revoked, "hall_script");
    REQUIRE(fixture.result(removed) == nullptr);
    REQUIRE(fixture.result(revoked) == nullptr);
    REQUIRE(fixture.frame(110));
    REQUIRE(fixture.result(removed) == nullptr);
    REQUIRE(fixture.result(revoked) == nullptr);
}

TEST_CASE("editing a single-readable script deletes no intents implicitly", "[l2.1]") {
    SelectionWorld fixture(110);
    fixture.add_light("office");
    fixture.add_script("main", proposing_source("office", "kept_level", 40));
    const BehaviorId behavior = fixture.behavior_on("office");
    fixture.grant_script(behavior, "main", ComponentAccessMode::ReadWrite);
    fixture.use_single_readable();

    REQUIRE(fixture.frame(100));
    REQUIRE(fixture.result(behavior) != nullptr);
    REQUIRE(fixture.result(behavior)->succeeded());
    const std::optional<IntentId> kept = fixture.world.intent_named(behavior, "kept_level");
    REQUIRE(kept.has_value());
    REQUIRE(fixture.level(behavior, "office") == 40);

    fixture.world.replace_component(
        fixture.scriptType, behavior, "main",
        LuaBehaviorScript{watching_source("office"), 2});
    REQUIRE(fixture.frame(110));
    REQUIRE(ran_on_start(fixture.result(behavior)));
    REQUIRE(fixture.world.intent_exists(*kept));
    REQUIRE(fixture.world.intent_named(behavior, "kept_level") == kept);
    REQUIRE(fixture.world.intent_count(behavior) == 1);
    REQUIRE(fixture.level(behavior, "office") == 40);
}

TEST_CASE("the named lifecycle constructor keeps fixed-name selection", "[l2.1]") {
    SelectionWorld fixture(111);
    fixture.add_light("office");
    fixture.add_light("hall");
    fixture.add_script("other", proposing_source("office", "other_level", 30));
    fixture.add_script("lifecycle", proposing_source("hall", "hall_level", 22));
    fixture.add_script("extra", proposing_source("hall", "extra_level", 60));
    const BehaviorId unnamed = fixture.behavior_on("office");
    fixture.grant_script(unnamed, "other");
    const BehaviorId named = fixture.behavior_on("hall");
    fixture.grant_script(named, "lifecycle");
    fixture.grant_script(named, "extra");
    fixture.world.register_system<LuaLifecycleSystem>(
        script_signature(fixture.scriptType),
        SystemPhase::Behavior,
        fixture.scriptType,
        fixture.runner);

    REQUIRE(fixture.frame(100));
    // A missing fixed-name script leaves no result; extra readable slots are
    // ignored because only the named slot is consulted.
    REQUIRE(fixture.result(unnamed) == nullptr);
    REQUIRE(fixture.world.intent_count(unnamed) == 0);
    REQUIRE(fixture.result(named) != nullptr);
    REQUIRE(fixture.result(named)->succeeded());
    REQUIRE(fixture.world.intent_named(named, "hall_level").has_value());
    REQUIRE(!fixture.world.intent_named(named, "extra_level").has_value());
}

TEST_CASE("the named lifecycle constructor keys state by source and revision only", "[l2.1]") {
    SelectionWorld fixture(114);
    fixture.add_light("office");
    const std::string source = watching_source("office");
    fixture.add_script("lifecycle", source);
    fixture.add_script("spare", watching_source("office", "-- spare\n"));
    const BehaviorId behavior = fixture.behavior_on("office");
    fixture.grant_script(behavior, "lifecycle");
    fixture.grant_script(behavior, "spare", ComponentAccessMode::Write);
    fixture.world.register_system<LuaLifecycleSystem>(
        script_signature(fixture.scriptType),
        SystemPhase::Behavior,
        fixture.scriptType,
        fixture.runner);

    REQUIRE(fixture.frame(100));
    REQUIRE(ran_on_start(fixture.result(behavior)));

    const ComponentSlotId previous =
        fixture.world.get_components(fixture.scriptType, behavior).at("lifecycle");
    fixture.world.remove_component(fixture.scriptType, "lifecycle");
    fixture.add_script("lifecycle", source);
    fixture.grant_script(behavior, "lifecycle");
    REQUIRE(fixture.world.get_components(fixture.scriptType, behavior).at("lifecycle") !=
        previous);

    // Pre-L2 behavior: an unchanged source and revision in a new slot keeps
    // the started lifecycle.
    REQUIRE(fixture.frame(110));
    REQUIRE(ran_without_start(fixture.result(behavior)));
}

TEST_CASE("the single-readable constructor validates like the named constructor", "[l2.1]") {
    World world;
    const auto scriptType = world.register_component<LuaBehaviorScript>(
        "liquid.LuaBehaviorScript", 1, lua_behavior_script_codec());
    const auto runner = std::make_shared<LuaBehaviorRunner>();

    REQUIRE_THROWS_AS(
        LuaLifecycleSystem(
            ComponentType<LuaBehaviorScript>{}, runner, LuaScriptSelection::SingleReadable),
        std::invalid_argument);
    REQUIRE_THROWS_AS(
        LuaLifecycleSystem(scriptType, nullptr, LuaScriptSelection::SingleReadable),
        std::invalid_argument);
    REQUIRE_THROWS_AS(
        LuaLifecycleSystem(scriptType, runner, static_cast<LuaScriptSelection>(7)),
        std::invalid_argument);
    REQUIRE_NOTHROW(
        LuaLifecycleSystem(scriptType, runner, LuaScriptSelection::SingleReadable));
}

TEST_CASE("single-readable scripts have no binding to their own script component", "[l2.1]") {
    SelectionWorld fixture(112);
    fixture.add_light("office");
    fixture.add_script("main", R"lua(
function on_start(frame)
    assert(access.LuaBehaviorScript == nil)
    assert(access["liquid.LuaBehaviorScript"] == nil)
    assert(access.Light.main == nil)
end
function on_frame(frame) end
)lua");
    const BehaviorId behavior = fixture.behavior_on("office");
    fixture.grant_script(behavior, "main");
    fixture.use_single_readable();

    REQUIRE(!fixture.world.can_write_component(fixture.scriptType, behavior, "main"));
    REQUIRE(fixture.frame(100));
    REQUIRE(fixture.result(behavior) != nullptr);
    INFO(fixture.result(behavior)->diagnostic);
    REQUIRE(fixture.result(behavior)->succeeded());
}

TEST_CASE("single-readable bundle failures still commit no proposals or watches", "[l2.1]") {
    SelectionWorld fixture(113);
    fixture.add_light("office");
    fixture.add_script("main", R"lua(
function on_start(frame)
    solid.watch(access.Light.office)
end
function on_frame(frame)
    access.Light.office.propose{name = "never_committed", value = {level = 70}}
    error("callback failed")
end
)lua");
    const BehaviorId behavior = fixture.behavior_on("office");
    fixture.grant_script(behavior, "main");
    fixture.use_single_readable();

    REQUIRE(fixture.frame(100));
    REQUIRE(fixture.result(behavior) != nullptr);
    REQUIRE(fixture.result(behavior)->status == LuaExecutionStatus::RuntimeError);
    REQUIRE(fixture.result(behavior)->watches.empty());
    REQUIRE(fixture.world.intent_count(behavior) == 0);
    REQUIRE(fixture.level(behavior, "office") == 10);

    // The failed bundle never started the lifecycle, so on_start runs again.
    REQUIRE(fixture.frame(110));
    REQUIRE(fixture.result(behavior)->status == LuaExecutionStatus::RuntimeError);
    REQUIRE(fixture.world.intent_count(behavior) == 0);
}

TEST_CASE("a stale script type handle gives the unavailable selection diagnostic", "[l2.1]") {
    SelectionWorld fixture(115);
    fixture.add_light("office");
    fixture.add_light("hall");
    fixture.add_script("main", proposing_source("office", "office_level", 30));
    fixture.add_script("hall_script", proposing_source("hall", "hall_level", 22));
    const BehaviorId behavior = fixture.behavior_on("office");
    fixture.grant_script(behavior, "main");
    const BehaviorId other = fixture.behavior_on("hall");
    fixture.grant_script(other, "hall_script");

    // World::get_components rejects a handle whose generation does not match
    // the registration, so selection throws for every member behavior.
    ComponentType<LuaBehaviorScript> stale = fixture.scriptType;
    stale.generation = fixture.scriptType.generation + 1;
    REQUIRE_THROWS(fixture.world.get_components(stale, behavior));
    Signature lights;
    lights.set(fixture.lightType.id);
    fixture.world.register_system<LuaLifecycleSystem>(
        lights,
        SystemPhase::Behavior,
        stale,
        fixture.runner,
        LuaScriptSelection::SingleReadable);

    for (IntentTime now : {IntentTime{100}, IntentTime{110}}) {
        REQUIRE(fixture.frame(now));
        REQUIRE(is_selection_failure(fixture.result(behavior), UnavailableScript));
        REQUIRE(is_selection_failure(fixture.result(other), UnavailableScript));
        REQUIRE(fixture.world.intent_count(behavior) == 0);
        REQUIRE(fixture.world.intent_count(other) == 0);
        REQUIRE(fixture.level(behavior, "office") == 10);
        REQUIRE(fixture.level(other, "hall") == 10);
    }
}
