#include "liquid/Runtime.hpp"
#include "liquid/authoring/AuthoringSession.hpp"
#include "liquid/authoring/Evaluation.hpp"
#include "liquid/authoring/Types.hpp"
#include "liquid/events/MemoryEventStore.hpp"
#include "liquid/scripting/LuaCapabilityManifest.hpp"
#include "liquid/scripting/LuaLifecycleSystem.hpp"
#include "liquid/simulation/InMemoryAdapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace liquid;
using namespace liquid::authoring;
using namespace liquid::scripting;
using namespace liquid::simulation;

namespace {

using Schema = LuaValueSchema;

// ---- Host-owned lighting-v1 definitions, shared by the live host and every
// ---- isolated case factory so both register the same versioned codecs.

struct Light {
    std::int64_t level = 0;
};

struct Counter {
    std::int64_t count = 0;
};

constexpr std::string_view LightRoute = "lighting.light";
constexpr std::string_view OfficeDevice = "office-device";
constexpr std::int64_t InitialOffice = 10;
constexpr IntentTime ScopeTime = 10;

struct LightingTypes {
    ComponentType<Light> light;
    ComponentType<Counter> counter;
    ComponentType<LuaBehaviorScript> script;
};

LightingTypes register_lighting_types(World& world) {
    LightingTypes types{
        world.register_component<Light>("test.Light", 1, ComponentCodec<Light>{
            [](const Light& light) { return Value{light.level}; },
            [](const Value& value) { return Light{value.as_signed_integer()}; }}),
        world.register_component<Counter>("test.Counter", 1, ComponentCodec<Counter>{
            [](const Counter& counter) { return Value{counter.count}; },
            [](const Value& value) { return Counter{value.as_signed_integer()}; }}),
        world.register_component<LuaBehaviorScript>(
            "liquid.LuaBehaviorScript", 1, lua_behavior_script_codec())};
    world.register_effect_codec(types.light, EffectCodec<Light>{
        AdapterRoute{std::string(LightRoute)},
        [](const ComponentName& name, const Light& light) -> std::optional<ResolvedEffect> {
            return ResolvedEffect{
                AdapterRoute{std::string(LightRoute)}, EffectTarget{name + "-device"}, Value{light.level}};
        },
        [](const Value& observed) { return Light{observed.as_signed_integer()}; }});
    return types;
}

Schema level_schema(std::int64_t maximum = 100) {
    return Schema::object({{"level", Schema::integer(0, maximum), true}});
}

Schema count_schema() {
    return Schema::object({{"count", Schema::integer(0, 1'000'000), true}});
}

void expose_lighting(LuaBehaviorRunner& runner, const LightingTypes& types, std::int64_t lightMaximum = 100) {
    runner.expose_component(types.light, "Light", LuaComponentCodec<Light>{
        [](const Light& light) { return LuaValue{LuaValue::Table{{"level", LuaValue{light.level}}}}; },
        [](const LuaValue& value) { return Light{value.as_table().at("level").as_integer()}; }},
        symmetric_metadata(level_schema(lightMaximum), "Brightness"));
    runner.expose_component(types.counter, "Counter", LuaComponentCodec<Counter>{
        [](const Counter& counter) { return LuaValue{LuaValue::Table{{"count", LuaValue{counter.count}}}}; },
        [](const LuaValue& value) { return Counter{value.as_table().at("count").as_integer()}; }},
        symmetric_metadata(count_schema(), "Frame counter"));
}

std::vector<ScopeGrant> lighting_grants() {
    return {
        ScopeGrant{"Light", "office", ComponentAccessMode::ReadWrite},
        ScopeGrant{"Light", "porch", ComponentAccessMode::Write},
        ScopeGrant{"Counter", "memory", ComponentAccessMode::ReadWrite}};
}

Value level_value(std::int64_t level) {
    return Value{Value::Object{{"level", Value{level}}}};
}

Value count_value(std::int64_t count) {
    return Value{Value::Object{{"count", Value{count}}}};
}

Value case_inputs(std::int64_t initialLevel, std::optional<std::int64_t> deadline = std::nullopt) {
    Value::Object inputs{{"initialLevel", Value{initialLevel}}};
    if (deadline)
        inputs.emplace("deadline", Value{*deadline});
    return Value{std::move(inputs)};
}

std::int64_t input_integer(const Value& inputs, const std::string& key, std::int64_t fallback) {
    if (inputs.kind() != Value::Kind::Object)
        return fallback;
    const auto found = inputs.as_object().find(key);
    return found == inputs.as_object().end() ? fallback : found->second.as_signed_integer();
}

// ---- Live host: the session's Runtime, World, runner, event store and a
// ---- stand-in production adapter that must never see evaluation work.

EventStoreMetadata live_metadata() {
    EventStoreMetadata metadata;
    metadata.session = SessionId{77};
    metadata.engineVersion = "0.1.0";
    metadata.feedbackTiming = FeedbackTiming::Deferred;
    return metadata;
}

RuntimeOptions live_options(MemoryEventStore& store) {
    RuntimeOptions options;
    options.sessionId = store.metadata().session;
    options.feedbackTiming = store.metadata().feedbackTiming;
    options.eventStore = &store;
    return options;
}

struct Host {
    MemoryEventStore store;
    Runtime runtime;
    World& world;
    LightingTypes types;
    LuaBehaviorRunner runner;
    std::shared_ptr<InMemoryAdapter> productionAdapter;
    BehaviorId resident;

    Host()
        : store(live_metadata()),
          runtime(live_options(store)),
          world(runtime.world()),
          types(register_lighting_types(world)) {
        expose_lighting(runner, types);
        world.add_component(types.light, "office", Light{40});
        world.add_component(types.light, "hallway", Light{5});
        world.add_component(types.light, "porch", Light{12});
        world.add_component(types.counter, "memory", Counter{900});
        productionAdapter = std::make_shared<InMemoryAdapter>(AdapterRoute{std::string(LightRoute)});
        runtime.register_adapter(productionAdapter);
        runtime.bind_effect_component(types.light, "office", EffectTarget{std::string(OfficeDevice)});
        resident = world.create_behavior();
        world.grant_component_access(types.light, resident, "office", ComponentAccessMode::ReadWrite);
        world.create_intent(
            resident,
            types.light,
            world.get_components(types.light, resident).at("office"),
            IntentLifetime::persistent(),
            Light{40},
            IntentPriority::Medium,
            "resident");
    }
};

// Everything externally visible about the live host.
struct LiveSnapshot {
    std::size_t behaviors = 0;
    std::vector<IntentId> intents;
    std::int64_t office = 0;
    std::int64_t hallway = 0;
    std::int64_t porch = 0;
    std::int64_t memory = 0;
    std::size_t componentMutations = 0;
    std::size_t topologyMutations = 0;
    std::vector<EventRecord> records;
    FrameNumber frame = 0;
    bool faulted = false;
    std::size_t adapterPending = 0;
    std::optional<Value> adapterState;
};

LiveSnapshot snapshot(Host& host) {
    LiveSnapshot result;
    result.behaviors = host.world.behavior_count();
    result.intents = host.world.live_intent_ids();
    result.office = host.world.get_component_named(host.types.light, "office")->level;
    result.hallway = host.world.get_component_named(host.types.light, "hallway")->level;
    result.porch = host.world.get_component_named(host.types.light, "porch")->level;
    result.memory = host.world.get_component_named(host.types.counter, "memory")->count;
    result.componentMutations = host.world.component_mutations().size();
    result.topologyMutations = host.world.topology_mutations().size();
    result.records = host.store.read_all();
    result.frame = host.runtime.frame();
    result.faulted = host.runtime.faulted();
    result.adapterPending = host.productionAdapter->pending();
    result.adapterState = host.productionAdapter->state(EffectTarget{std::string(OfficeDevice)});
    return result;
}

void require_unchanged(const LiveSnapshot& before, const LiveSnapshot& after) {
    CHECK(after.behaviors == before.behaviors);
    CHECK(after.intents == before.intents);
    CHECK(after.office == before.office);
    CHECK(after.hallway == before.hallway);
    CHECK(after.porch == before.porch);
    CHECK(after.memory == before.memory);
    CHECK(after.componentMutations == before.componentMutations);
    CHECK(after.topologyMutations == before.topologyMutations);
    CHECK(after.records == before.records);
    CHECK(after.frame == before.frame);
    CHECK(after.faulted == before.faulted);
    CHECK(after.adapterPending == before.adapterPending);
    CHECK(after.adapterState == before.adapterState);
}

// ---- Isolated case factory.

// Fresh handles of the most recent isolated world a factory built; the case's
// input and expectation callbacks read them.
struct Isolated {
    ComponentType<Light> light;
    ComponentType<Counter> counter;
    BehaviorId candidate{};
    std::optional<BehaviorId> competitor;
    std::optional<IntentId> competitorIntent;
    std::optional<IntentId> keepIntent;
    std::vector<ScopeGrant> grantsSeen;
    std::string sourceSeen;
    Value inputsSeen;
    std::size_t adaptersSeen = 0;
    const EventStore* storeSeen = nullptr;
    FeedbackTiming timingSeen = FeedbackTiming::Immediate;
    const World* liveWorld = nullptr;
    const World* isolatedWorld = nullptr;
    std::size_t prepareCalls = 0;
};

// Deliberate deviations from the live host, for mismatch and containment cases.
struct FactoryVariant {
    bool extraHallwayRead = false;
    std::int64_t lightMaximum = 100;
    std::optional<ComponentAccessMode> officeMode;
    bool throwOnPrepare = false;
    bool omitScriptGrant = false;
};

void grant_by_name(World& world, const LightingTypes& types, BehaviorId behavior, const ScopeGrant& grant) {
    if (grant.scriptTypeName == "Light")
        world.grant_component_access(types.light, behavior, grant.componentName, grant.mode);
    else if (grant.scriptTypeName == "Counter")
        world.grant_component_access(types.counter, behavior, grant.componentName, grant.mode);
    else
        throw std::invalid_argument("lighting-v1 has no binding " + grant.scriptTypeName);
}

void wire_adapters(PreparedEvaluation& prepared, const LightingTypes& types, const EvaluationPreparation& prep) {
    if (prep.adapters.empty()) {
        prepared.runtime->configure_component(types.light, "office", ComponentControl::InternalState);
        return;
    }
    for (const AdapterBehavior& behavior : prep.adapters) {
        auto adapter = std::make_shared<InMemoryAdapter>(AdapterRoute{std::string(LightRoute)}, behavior);
        prepared.runtime->register_adapter(adapter);
        prepared.adapters.push_back(std::move(adapter));
    }
    prepared.runtime->bind_effect_component(types.light, "office", EffectTarget{std::string(OfficeDevice)});
    for (const ScopeGrant& grant : prep.grants) {
        if (grant.scriptTypeName == "Light" && grant.componentName == "office")
            prepared.grantedEffects.push_back(
                {"Light", "office", AdapterRoute{std::string(LightRoute)}, EffectTarget{std::string(OfficeDevice)}});
    }
}

EvaluationCaseFactory lighting_factory(
    std::shared_ptr<Isolated> isolated,
    const World* liveWorld,
    FactoryVariant variant = {}
) {
    return [isolated, liveWorld, variant](const EvaluationPreparation& prep) {
        ++isolated->prepareCalls;
        if (variant.throwOnPrepare)
            throw std::runtime_error("host factory failed");
        isolated->grantsSeen = prep.grants;
        isolated->sourceSeen = prep.source;
        isolated->inputsSeen = prep.inputs;
        isolated->adaptersSeen = prep.adapters.size();
        isolated->storeSeen = prep.runtimeOptions.eventStore;
        isolated->timingSeen = prep.runtimeOptions.feedbackTiming;
        isolated->liveWorld = liveWorld;
        isolated->competitor.reset();
        isolated->competitorIntent.reset();
        isolated->keepIntent.reset();

        PreparedEvaluation prepared;
        prepared.runtime = std::make_unique<Runtime>(prep.runtimeOptions);
        World& world = prepared.runtime->world();
        const LightingTypes types = register_lighting_types(world);
        world.add_component(types.light, "office", Light{input_integer(prep.inputs, "initialLevel", InitialOffice)});
        world.add_component(types.light, "hallway", Light{5});
        // Private Write-only baseline; never reported back.
        world.add_component(types.light, "porch", Light{33});
        world.add_component(types.counter, "memory", Counter{0});
        world.add_component(types.script, "lifecycle", LuaBehaviorScript{prep.source, 1});

        prepared.candidate = world.create_behavior();
        for (ScopeGrant grant : prep.grants) {
            if (variant.officeMode && grant.componentName == "office")
                grant.mode = *variant.officeMode;
            grant_by_name(world, types, prepared.candidate, grant);
        }
        if (variant.extraHallwayRead)
            world.grant_component_access(types.light, prepared.candidate, "hallway", ComponentAccessMode::Read);
        if (!variant.omitScriptGrant)
            world.grant_component_access(types.script, prepared.candidate, "lifecycle", ComponentAccessMode::Read);

        prepared.runner = std::make_shared<LuaBehaviorRunner>();
        expose_lighting(*prepared.runner, types, variant.lightMaximum);
        Signature signature;
        signature.set(types.script.id);
        world.register_system<LuaLifecycleSystem>(
            signature, SystemPhase::Behavior, types.script, prepared.runner, LuaScriptSelection::SingleReadable);
        prepared.runtime->configure_component(types.counter, "memory", ComponentControl::InternalState);
        wire_adapters(prepared, types, prep);
        prepared.scriptType = types.script;

        isolated->light = types.light;
        isolated->counter = types.counter;
        isolated->candidate = prepared.candidate;
        isolated->isolatedWorld = &world;
        return prepared;
    };
}

// ---- Expectation helpers (trusted host code).

class Verdict {
public:
    Verdict& require(bool condition, std::string_view what) {
        if (passed && !condition) {
            passed = false;
            diagnostic = std::string(what);
        }
        return *this;
    }

    EvaluationAssertion done() const {
        return EvaluationAssertion{passed, passed ? std::string{} : diagnostic};
    }

    bool ok() const {
        return passed;
    }

private:
    bool passed = true;
    std::string diagnostic;
};

std::int64_t level_of(const PreparedEvaluation& prepared, const Isolated& isolated, const std::string& name = "office") {
    const Light* light = prepared.runtime->world().get_component_named(isolated.light, name);
    return light ? light->level : -1;
}

std::int64_t count_of(const PreparedEvaluation& prepared, const Isolated& isolated) {
    const Counter* counter = prepared.runtime->world().get_component_named(isolated.counter, "memory");
    return counter ? counter->count : -1;
}

std::optional<IntentId> named_intent(const PreparedEvaluation& prepared, const std::string& name) {
    return prepared.runtime->world().intent_named(prepared.candidate, name);
}

std::optional<IntentId> logged_light_selection(const Isolated& isolated, const EvaluationFrameContext& context) {
    if (!context.frame)
        return std::nullopt;
    const auto& selections = context.frame->frame.intent_selections;
    const auto type = selections.find(isolated.light.id);
    if (type == selections.end())
        return std::nullopt;
    const auto slot = type->second.find("office");
    if (slot == type->second.end())
        return std::nullopt;
    return slot->second;
}

std::size_t report_count(const EvaluationFrameContext& context, CommandStatus status) {
    if (!context.frame)
        return 0;
    return static_cast<std::size_t>(std::count_if(
        context.frame->reports.begin(), context.frame->reports.end(),
        [status](const EffectReport& report) { return report.status == status; }));
}

bool frame_issued(const EvaluationFrameContext& context, std::int64_t desired) {
    if (!context.frame)
        return false;
    return std::any_of(context.frame->commands.begin(), context.frame->commands.end(),
        [desired](const EffectCommand& command) {
            return command.effect.adapterRoute == AdapterRoute{std::string(LightRoute)}
                && command.effect.target == EffectTarget{std::string(OfficeDevice)}
                && command.effect.desiredValue == Value{desired};
        });
}

bool recorded(const EvaluationFrameContext& context, EventType type) {
    return std::any_of(context.frameRecords.begin(), context.frameRecords.end(),
        [type](const EventRecord& record) { return record.type == type; });
}

Verdict base_verdict(const EvaluationFrameContext& context) {
    Verdict verdict;
    verdict.require(context.frame != nullptr, "frame result missing")
        .require(context.frame && context.frame->frame.completed, "frame did not complete")
        .require(context.lifecycle != nullptr, "lifecycle result missing")
        .require(context.lifecycle && context.lifecycle->succeeded(), "lifecycle failed")
        .require(context.inputs != nullptr, "inputs missing");
    return verdict;
}

// ---- Reference candidate and negative proposals.

// `keep` (low, persistent) and `boost` (medium, 5 ms) both select 70; `tick`
// carries Counter state across fresh VMs (a reused VM would fail the assert).
const std::string ReferenceSource = R"lua(
function on_start(frame)
    solid.watch(access.Light.office)
    access.Light.office.propose{name = "keep", value = {level = 70}, priority = "low", lifetime = "persistent"}
    access.Light.office.propose{name = "boost", value = {level = 70}, priority = "medium", duration_ms = 5}
end

function on_frame(frame)
    assert(fresh_vm_marker == nil)
    fresh_vm_marker = true
    local tick = solid.owned_intents.tick
    if tick ~= nil then
        solid.cancel(tick)
    end
    access.Counter.memory.propose{
        name = "tick",
        value = {count = access.Counter.memory.value.count + 1},
        priority = "medium",
        lifetime = "persistent"
    }
end
)lua";

const std::string SyntaxErrorSource = "function on_start(frame)\n    access.Light.office.propose{\nend\n";

const std::string RuntimeErrorSource = R"lua(
function on_start(frame)
    error("candidate exploded")
end
)lua";

const std::string ForbiddenAccessSource = R"lua(
function on_start(frame)
    access.Light.hallway.propose{name = "hallway", value = {level = 70}, priority = "low", lifetime = "persistent"}
end
)lua";

const std::string InvalidCodecSource = R"lua(
function on_start(frame)
    access.Light.office.propose{name = "bad", value = {level = "bright"}, priority = "low", lifetime = "persistent"}
end
)lua";

// Frame 0 commits `keep`; frame 1 cancels it and proposes a duplicate-named
// replacement, so the whole bundle must roll back and `keep` survive.
const std::string FailedBundleSource = R"lua(
function on_start(frame)
    access.Light.office.propose{name = "keep", value = {level = 70}, priority = "low", lifetime = "persistent"}
end

function on_frame(frame)
    if frame.number == 1 then
        solid.cancel(solid.owned_intents.keep)
        access.Light.office.propose{name = "replacement", value = {level = 40}, priority = "low", lifetime = "persistent"}
        access.Light.office.propose{name = "replacement", value = {level = 45}, priority = "low", lifetime = "persistent"}
    end
end
)lua";

const std::string InstructionLimitSource = R"lua(
function on_start(frame)
    while true do end
end
)lua";

const std::string MemoryLimitSource = R"lua(
function on_start(frame)
    local text = string.rep("x", 1024)
    while true do
        text = text .. text
    end
end
)lua";

// ---- lighting-v1 suite.

const EvaluationFixtureId LightingV1{"lighting-v1", 1};

enum LightingCase : std::size_t {
    PersistentCase,
    CompetitorCase,
    ExpiryCase,
    InternalStateCase,
    DeferredCase,
    RejectedCase,
    SilentCase,
    DuplicateCase,
    DelayedCase,
    ConnectivityCase,
    LightingCaseCount
};

const std::vector<IntentTime> StandardFrames{0, 10, 20};
constexpr std::int64_t ExpiryDeadline = 5;

EvaluationCase lighting_case(
    std::string name,
    const std::shared_ptr<Isolated>& isolated,
    const World* liveWorld,
    const FactoryVariant& variant
) {
    EvaluationCase evaluationCase;
    evaluationCase.name = std::move(name);
    evaluationCase.inputs = case_inputs(InitialOffice);
    evaluationCase.frameTimes = StandardFrames;
    evaluationCase.prepare = lighting_factory(isolated, liveWorld, variant);
    return evaluationCase;
}

EvaluationCase persistent_case(const World* liveWorld, const FactoryVariant& variant) {
    auto isolated = std::make_shared<Isolated>();
    EvaluationCase evaluationCase = lighting_case("persistent 70 success", isolated, liveWorld, variant);
    evaluationCase.expect = [isolated](const PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
        Verdict verdict = base_verdict(context);
        const std::optional<IntentId> selected = logged_light_selection(*isolated, context);
        verdict.require(level_of(prepared, *isolated) == 70, "office is not 70")
            .require(named_intent(prepared, "keep").has_value(), "persistent keep intent missing")
            .require(!selected || prepared.runtime->world().intent_owner(*selected) == prepared.candidate,
                "office selection is not the candidate's")
            .require(recorded(context, EventType::ScriptExecuted), "no ScriptExecuted evidence in the test store");
        return verdict.done();
    };
    return evaluationCase;
}

EvaluationCase competitor_case(const World* liveWorld, const FactoryVariant& variant) {
    auto isolated = std::make_shared<Isolated>();
    EvaluationCase evaluationCase =
        lighting_case("competing higher-priority 0 then reselection of the original 70", isolated, liveWorld, variant);
    evaluationCase.input = [isolated](PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
        World& world = prepared.runtime->world();
        if (context.frameIndex == 1) {
            const BehaviorId competitor = world.create_behavior();
            world.grant_component_access(isolated->light, competitor, "office", ComponentAccessMode::ReadWrite);
            isolated->competitor = competitor;
            isolated->competitorIntent = world.create_intent(
                competitor,
                isolated->light,
                world.get_components(isolated->light, competitor).at("office"),
                IntentLifetime::persistent(),
                Light{0},
                IntentPriority::High,
                "override");
        } else if (context.frameIndex == 2 && isolated->competitorIntent) {
            world.destroy_intent(*isolated->competitorIntent);
        }
    };
    evaluationCase.expect = [isolated](const PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
        Verdict verdict = base_verdict(context);
        const std::optional<IntentId> keep = named_intent(prepared, "keep");
        if (context.frameIndex == 0) {
            isolated->keepIntent = keep;
            verdict.require(keep.has_value(), "keep intent missing").require(level_of(prepared, *isolated) == 70, "office is not 70");
        } else if (context.frameIndex == 1) {
            verdict.require(level_of(prepared, *isolated) == 0, "higher-priority competitor 0 did not win")
                .require(keep == isolated->keepIntent, "original keep intent was replaced");
        } else {
            const std::optional<IntentId> selected = logged_light_selection(*isolated, context);
            verdict.require(level_of(prepared, *isolated) == 70, "original 70 was not reselected")
                .require(keep.has_value() && keep == isolated->keepIntent, "reselected intent is not the original keep")
                .require(!selected || selected == isolated->keepIntent, "logged selection is not the original keep");
        }
        return verdict.done();
    };
    return evaluationCase;
}

EvaluationCase expiry_case(const World* liveWorld, const FactoryVariant& variant) {
    auto isolated = std::make_shared<Isolated>();
    EvaluationCase evaluationCase = lighting_case("until-time expiration", isolated, liveWorld, variant);
    evaluationCase.inputs = case_inputs(InitialOffice, ExpiryDeadline);
    // The deadline and the frame immediately after it.
    evaluationCase.frameTimes = {0, static_cast<IntentTime>(ExpiryDeadline - 1), static_cast<IntentTime>(ExpiryDeadline + 1)};
    evaluationCase.expect = [isolated](const PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
        Verdict verdict = base_verdict(context);
        if (!verdict.ok())
            return verdict.done();
        const auto deadline = static_cast<IntentTime>(input_integer(*context.inputs, "deadline", -1));
        const bool boostLive = named_intent(prepared, "boost").has_value();
        verdict.require(deadline == static_cast<IntentTime>(ExpiryDeadline), "fixture deadline not visible to expectations")
            .require(boostLive == (context.now < deadline), "boost did not expire exactly at its deadline")
            .require(named_intent(prepared, "keep").has_value(), "persistent keep expired")
            .require(level_of(prepared, *isolated) == 70, "office is not 70");
        return verdict.done();
    };
    return evaluationCase;
}

EvaluationCase internal_state_case(const World* liveWorld, const FactoryVariant& variant) {
    auto isolated = std::make_shared<Isolated>();
    EvaluationCase evaluationCase = lighting_case("internal-state persistence across fresh VMs", isolated, liveWorld, variant);
    evaluationCase.expect = [isolated](const PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
        Verdict verdict = base_verdict(context);
        verdict.require(count_of(prepared, *isolated) == static_cast<std::int64_t>(context.frameIndex) + 1,
            "Counter did not advance once per fresh VM");
        return verdict.done();
    };
    return evaluationCase;
}

EvaluationCase adapter_case(
    std::string name,
    AdapterBehavior behavior,
    const World* liveWorld,
    const FactoryVariant& variant,
    std::shared_ptr<Isolated>& isolated
) {
    isolated = std::make_shared<Isolated>();
    EvaluationCase evaluationCase = lighting_case(std::move(name), isolated, liveWorld, variant);
    evaluationCase.adapters = {std::move(behavior)};
    return evaluationCase;
}

std::optional<Value> device_state(const PreparedEvaluation& prepared) {
    if (prepared.adapters.empty())
        return std::nullopt;
    return prepared.adapters.front()->state(EffectTarget{std::string(OfficeDevice)});
}

EvaluationCase deferred_case(const World* liveWorld, const FactoryVariant& variant) {
    std::shared_ptr<Isolated> isolated;
    EvaluationCase evaluationCase = adapter_case("deferred feedback", AdapterBehavior{}, liveWorld, variant, isolated);
    evaluationCase.expect = [isolated](const PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
        Verdict verdict = base_verdict(context);
        if (context.frameIndex == 0) {
            verdict.require(frame_issued(context, 70), "no command for 70")
                .require(level_of(prepared, *isolated) == InitialOffice, "deferred report projected in the issuing frame")
                .require(device_state(prepared) == std::optional<Value>{Value{std::int64_t{70}}}, "device did not apply 70");
        } else if (context.frameIndex == 1) {
            verdict.require(report_count(context, CommandStatus::Applied) == 1, "Applied report not received")
                .require(level_of(prepared, *isolated) == 70, "Applied report not projected");
        } else {
            verdict.require(level_of(prepared, *isolated) == 70, "office drifted from 70");
        }
        return verdict.done();
    };
    return evaluationCase;
}

EvaluationCase rejected_case(const World* liveWorld, const FactoryVariant& variant) {
    std::shared_ptr<Isolated> isolated;
    AdapterBehavior behavior;
    behavior.outcome = CommandStatus::Rejected;
    EvaluationCase evaluationCase = adapter_case("rejected report", behavior, liveWorld, variant, isolated);
    evaluationCase.expect = [isolated](const PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
        Verdict verdict = base_verdict(context);
        if (context.frameIndex == 0)
            verdict.require(frame_issued(context, 70), "no command for 70");
        if (context.frameIndex == 1)
            verdict.require(report_count(context, CommandStatus::Rejected) == 1, "Rejected report not received");
        verdict.require(level_of(prepared, *isolated) == InitialOffice, "a rejected report was projected");
        return verdict.done();
    };
    return evaluationCase;
}

EvaluationCase silent_case(const World* liveWorld, const FactoryVariant& variant) {
    std::shared_ptr<Isolated> isolated;
    AdapterBehavior behavior;
    behavior.silent = true;
    EvaluationCase evaluationCase = adapter_case("silent adapter", behavior, liveWorld, variant, isolated);
    evaluationCase.expect = [isolated](const PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
        Verdict verdict = base_verdict(context);
        if (context.frameIndex == 0)
            verdict.require(frame_issued(context, 70), "no command for 70");
        verdict.require(context.frame && context.frame->reports.empty(), "silent adapter produced a report")
            .require(level_of(prepared, *isolated) == InitialOffice, "office changed without a report");
        return verdict.done();
    };
    return evaluationCase;
}

EvaluationCase duplicate_case(const World* liveWorld, const FactoryVariant& variant) {
    std::shared_ptr<Isolated> isolated;
    AdapterBehavior behavior;
    behavior.duplicateReports = 1;
    EvaluationCase evaluationCase = adapter_case("duplicate report", behavior, liveWorld, variant, isolated);
    evaluationCase.expect = [isolated](const PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
        Verdict verdict = base_verdict(context);
        verdict.require(context.frame && context.frame->reports.size() <= 1, "duplicate report was projected twice");
        if (context.frameIndex == 1)
            verdict.require(report_count(context, CommandStatus::Applied) == 1, "Applied report not received");
        if (context.frameIndex >= 1)
            verdict.require(level_of(prepared, *isolated) == 70, "Applied report not projected");
        return verdict.done();
    };
    return evaluationCase;
}

EvaluationCase delayed_case(const World* liveWorld, const FactoryVariant& variant) {
    std::shared_ptr<Isolated> isolated;
    AdapterBehavior behavior;
    behavior.latencyMs = 15;
    EvaluationCase evaluationCase = adapter_case("delayed report", behavior, liveWorld, variant, isolated);
    evaluationCase.expect = [isolated](const PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
        Verdict verdict = base_verdict(context);
        if (context.frameIndex < 2) {
            verdict.require(context.frame && context.frame->reports.empty(), "report arrived before its latency")
                .require(level_of(prepared, *isolated) == InitialOffice, "office changed before the report");
        } else {
            verdict.require(report_count(context, CommandStatus::Applied) == 1, "delayed report not delivered at 20")
                .require(level_of(prepared, *isolated) == 70, "delayed report not projected");
        }
        return verdict.done();
    };
    return evaluationCase;
}

EvaluationCase connectivity_case(const World* liveWorld, const FactoryVariant& variant) {
    std::shared_ptr<Isolated> isolated;
    EvaluationCase evaluationCase = adapter_case("no model connectivity", AdapterBehavior{}, liveWorld, variant, isolated);
    evaluationCase.expect = [isolated](const PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
        Verdict verdict = base_verdict(context);
        const bool inMemoryOnly = context.frame && std::all_of(
            context.frame->commands.begin(), context.frame->commands.end(),
            [](const EffectCommand& command) { return command.effect.adapterRoute == AdapterRoute{std::string(LightRoute)}; });
        verdict.require(inMemoryOnly, "command left the in-memory route")
            .require(prepared.adapters.size() == 1, "factory did not wire exactly the case adapter")
            .require(&prepared.runtime->world() != isolated->liveWorld, "evaluation ran in the live World")
            .require(isolated->storeSeen != nullptr, "isolated Runtime has no bounded test store");
        if (context.frameIndex == 0)
            verdict.require(device_state(prepared) == std::optional<Value>{Value{std::int64_t{70}}}, "in-memory device not driven");
        return verdict.done();
    };
    return evaluationCase;
}

EvaluationSuite lighting_suite(const World* liveWorld, EvaluationFixtureId id = LightingV1, FactoryVariant variant = {}) {
    EvaluationSuite suite;
    suite.id = std::move(id);
    suite.cases.push_back(persistent_case(liveWorld, variant));
    suite.cases.push_back(competitor_case(liveWorld, variant));
    suite.cases.push_back(expiry_case(liveWorld, variant));
    suite.cases.push_back(internal_state_case(liveWorld, variant));
    suite.cases.push_back(deferred_case(liveWorld, variant));
    suite.cases.push_back(rejected_case(liveWorld, variant));
    suite.cases.push_back(silent_case(liveWorld, variant));
    suite.cases.push_back(duplicate_case(liveWorld, variant));
    suite.cases.push_back(delayed_case(liveWorld, variant));
    suite.cases.push_back(connectivity_case(liveWorld, variant));
    return suite;
}

// Small two-case suite for containment and budget probes.
EvaluationSuite probe_suite(
    EvaluationFixtureId id,
    EvaluationCaseFactory factory,
    EvaluationInputAction input = {},
    EvaluationExpectation expect = {}
) {
    if (!expect)
        expect = [](const PreparedEvaluation&, const EvaluationFrameContext&) { return EvaluationAssertion{true, {}}; };
    EvaluationSuite suite;
    suite.id = std::move(id);
    for (const char* name : {"probe first", "probe second"}) {
        EvaluationCase evaluationCase;
        evaluationCase.name = name;
        evaluationCase.inputs = case_inputs(InitialOffice);
        evaluationCase.frameTimes = {0, 10};
        evaluationCase.prepare = factory;
        evaluationCase.input = input;
        evaluationCase.expect = expect;
        suite.cases.push_back(std::move(evaluationCase));
    }
    return suite;
}

// ---- Session harness.

AuthoringSessionId session_id(char digit = '0') {
    const auto id = AuthoringSessionId::from_hex(std::string(AuthoringSessionIdHexDigits, digit));
    REQUIRE(id.ok());
    return id.value();
}

template <typename T>
AuthoringError require_error(const AuthoringResult<T>& result, AuthoringErrorCode code) {
    REQUIRE_FALSE(result.ok());
    const AuthoringError& error = result.error();
    CHECK(error.code == code);
    CHECK_FALSE(error.diagnostic.empty());
    CHECK(error.diagnostic.size() + error.fieldPath.value_or("").size() <= AuthoringMaxDiagnosticBytes);
    return error;
}

struct Lab {
    Host host;
    AuthoringSession session;
    CallerContext alice;
    std::optional<ScopeView> scopeView;

    explicit Lab(AuthoringLimits limits = {})
        : session(session_id(), host.runtime, host.runner, host.types.script, limits),
          alice{session.id(), "alice"} {}

    void register_suite(EvaluationSuite suite) {
        const AuthoringResult<void> registered = session.register_evaluation_suite(std::move(suite));
        INFO((registered.ok() ? std::string{} : registered.error().diagnostic));
        REQUIRE(registered.ok());
    }

    void register_lighting() {
        register_suite(lighting_suite(&host.world));
    }

    const ScopeView& scope() {
        if (!scopeView) {
            const auto created = session.create_scope("alice", lighting_grants(), ScopeTime);
            INFO((created.ok() ? std::string{} : created.error().diagnostic));
            REQUIRE(created.ok());
            scopeView = created.value();
        }
        return *scopeView;
    }

    ProposalId submit(const std::string& source) {
        ProposalSubmission submission;
        submission.scope = scope().scope;
        submission.expectedScopeRevision = scope().revision;
        submission.source = source;
        const auto submitted = session.submit(alice, std::move(submission));
        INFO((submitted.ok() ? std::string{} : submitted.error().diagnostic));
        REQUIRE(submitted.ok());
        return submitted.value();
    }

    EvaluationRecord evaluate(ProposalId proposal, const EvaluationFixtureId& fixture = LightingV1) {
        const auto evaluated = session.evaluate(alice, proposal, fixture);
        INFO((evaluated.ok() ? std::string{} : evaluated.error().diagnostic));
        REQUIRE(evaluated.ok());
        return evaluated.value();
    }
};

const EvaluationTargetSummary* target(const EvaluationCaseResult& result, const std::string& component) {
    const auto found = std::find_if(result.finalState.begin(), result.finalState.end(),
        [&component](const EvaluationTargetSummary& summary) { return summary.componentName == component; });
    return found == result.finalState.end() ? nullptr : &*found;
}

void require_not_run_after(const EvaluationRecord& record, std::size_t failedCase) {
    REQUIRE(record.cases.size() > failedCase);
    for (std::size_t index = failedCase + 1; index < record.cases.size(); ++index) {
        INFO("case " << index << ": " << record.cases[index].name);
        CHECK(record.cases[index].status == EvaluationCaseStatus::NotRun);
        CHECK(record.cases[index].framesRun == 0);
        CHECK(record.cases[index].frames.empty());
    }
}

// The first case stopped at `frame` with `lifecycle`; every later case NotRun.
void require_first_case_stopped(
    const EvaluationRecord& record,
    EvaluationStatus status,
    EvaluationCaseStatus caseStatus,
    std::size_t framesRun
) {
    CHECK(record.status == status);
    CHECK(record.complete);
    REQUIRE(record.cases.size() == LightingCaseCount);
    const EvaluationCaseResult& first = record.cases.front();
    CHECK(first.status == caseStatus);
    CHECK(first.framesScheduled == StandardFrames.size());
    CHECK(first.framesRun == framesRun);
    CHECK(first.frames.size() == framesRun);
    CHECK_FALSE(first.diagnostic.empty());
    CHECK(first.diagnostic.size() <= AuthoringMaxDiagnosticBytes);
    require_not_run_after(record, 0);
}

std::optional<LuaExecutionStatus> last_lifecycle(const EvaluationRecord& record) {
    if (record.cases.empty() || record.cases.front().frames.empty())
        return std::nullopt;
    return record.cases.front().frames.back().lifecycleStatus;
}

}

// =====================================================================
// L2.2 — trusted suite registration and isolated preparation
// =====================================================================

TEST_CASE("L2.2 registration: suites register only before the first accepted proposal") {
    Lab lab;
    lab.register_lighting();
    lab.submit(ReferenceSource);

    const AuthoringError late = require_error(
        lab.session.register_evaluation_suite(lighting_suite(&lab.host.world, {"lighting-late", 1})),
        AuthoringErrorCode::Unsupported);
    (void)late;
    require_error(lab.session.evaluate(lab.alice, ProposalId{1}, {"lighting-late", 1}), AuthoringErrorCode::NotFound);
}

TEST_CASE("L2.2 registration: invalid suites are rejected with InvalidInput and register nothing") {
    Lab lab;
    const World* live = &lab.host.world;
    lab.register_lighting();

    const auto rejects = [&lab](EvaluationSuite suite, std::string_view why) {
        INFO(why);
        require_error(lab.session.register_evaluation_suite(std::move(suite)), AuthoringErrorCode::InvalidInput);
    };

    rejects(lighting_suite(live), "duplicate fixture id");
    rejects(lighting_suite(live, {"", 1}), "empty stable name");
    rejects(lighting_suite(live, {std::string(lab.session.limits().maxLabelBytes + 1, 'n'), 1}), "oversized stable name");
    rejects(lighting_suite(live, {"lighting-zero", 0}), "version 0");

    EvaluationSuite emptyCaseName = lighting_suite(live, {"bad-case-name", 1});
    emptyCaseName.cases[1].name.clear();
    rejects(std::move(emptyCaseName), "empty case name");

    EvaluationSuite longCaseName = lighting_suite(live, {"long-case-name", 1});
    longCaseName.cases[0].name = std::string(lab.session.limits().maxLabelBytes + 1, 'c');
    rejects(std::move(longCaseName), "oversized case name");

    EvaluationSuite nullPrepare = lighting_suite(live, {"null-prepare", 1});
    nullPrepare.cases[2].prepare = nullptr;
    rejects(std::move(nullPrepare), "null prepare");

    EvaluationSuite nullExpect = lighting_suite(live, {"null-expect", 1});
    nullExpect.cases[3].expect = nullptr;
    rejects(std::move(nullExpect), "null expect");

    EvaluationSuite noFrames = lighting_suite(live, {"no-frames", 1});
    noFrames.cases[0].frameTimes.clear();
    rejects(std::move(noFrames), "empty frameTimes");

    EvaluationSuite decreasing = lighting_suite(live, {"decreasing", 1});
    decreasing.cases[0].frameTimes = {10, 5};
    rejects(std::move(decreasing), "decreasing frameTimes");

    EvaluationSuite huge = lighting_suite(live, {"huge-time", 1});
    huge.cases[0].frameTimes = {0, static_cast<IntentTime>(std::numeric_limits<std::int64_t>::max()) + 1};
    rejects(std::move(huge), "frame time above INT64_MAX");

    // Failed registrations kept nothing: the same IDs are still free.
    lab.register_suite(lighting_suite(live, {"decreasing", 1}));
    lab.register_suite(lighting_suite(live, {"null-expect", 1}));

    // Equal neighbouring times are nondecreasing and accepted.
    EvaluationSuite repeated = lighting_suite(live, {"repeated-time", 1});
    repeated.cases[0].frameTimes = {0, 0, 10};
    lab.register_suite(std::move(repeated));
}

TEST_CASE("L2.2 registration: case and frame budgets are LimitExceeded") {
    AuthoringLimits limits;
    limits.maxEvaluationCases = 2;
    limits.maxEvaluationFrames = 2;
    Lab lab(limits);
    const World* live = &lab.host.world;
    auto isolated = std::make_shared<Isolated>();

    EvaluationSuite tooManyCases = lighting_suite(live, {"too-many-cases", 1});
    for (EvaluationCase& evaluationCase : tooManyCases.cases)
        evaluationCase.frameTimes = {0, 10};
    require_error(lab.session.register_evaluation_suite(std::move(tooManyCases)), AuthoringErrorCode::LimitExceeded);

    EvaluationSuite tooManyFrames = probe_suite({"too-many-frames", 1}, lighting_factory(isolated, live));
    tooManyFrames.cases[0].frameTimes = {0, 10, 20};
    require_error(lab.session.register_evaluation_suite(std::move(tooManyFrames)), AuthoringErrorCode::LimitExceeded);

    lab.register_suite(probe_suite({"within-budget", 1}, lighting_factory(isolated, live)));
}

TEST_CASE("L2.2 registration: unknown or wrong-version fixture IDs are NotFound and create no record") {
    Lab lab;
    lab.register_lighting();
    const ProposalId proposal = lab.submit(ReferenceSource);
    const LiveSnapshot before = snapshot(lab.host);

    require_error(lab.session.evaluate(lab.alice, proposal, {"lighting-v9", 1}), AuthoringErrorCode::NotFound);
    require_error(lab.session.evaluate(lab.alice, proposal, {"lighting-v1", 2}), AuthoringErrorCode::NotFound);
    require_error(lab.session.normalized_trace(EvaluationId{1}), AuthoringErrorCode::NotFound);
    require_unchanged(before, snapshot(lab.host));

    // No ID was consumed by the failures.
    const EvaluationRecord record = lab.evaluate(proposal);
    CHECK(record.id == EvaluationId{1});
}

TEST_CASE("L2.2 caller boundary: wrong session, wrong owner and unknown proposals are NotFound") {
    Lab lab;
    lab.register_lighting();
    const ProposalId proposal = lab.submit(ReferenceSource);

    require_error(lab.session.evaluate(CallerContext{session_id('1'), "alice"}, proposal, LightingV1), AuthoringErrorCode::NotFound);
    require_error(lab.session.evaluate(CallerContext{lab.session.id(), "mallory"}, proposal, LightingV1), AuthoringErrorCode::NotFound);
    require_error(lab.session.evaluate(lab.alice, ProposalId{99}, LightingV1), AuthoringErrorCode::NotFound);
    require_error(lab.session.evaluate(lab.alice, ProposalId{}, LightingV1), AuthoringErrorCode::NotFound);

    CHECK(lab.evaluate(proposal).id == EvaluationId{1});
}

TEST_CASE("L2.2 oracle: evaluation leaves live World counts, data, intents, effects, event store and clock unchanged") {
    Lab lab;
    lab.register_lighting();
    const ProposalId proposal = lab.submit(ReferenceSource);
    const LiveSnapshot before = snapshot(lab.host);

    const EvaluationRecord record = lab.evaluate(proposal);
    CHECK(record.status == EvaluationStatus::Passed);
    require_unchanged(before, snapshot(lab.host));

    // Frames ran up to now=20 in isolation; the live session clock is still 10.
    const auto discovered = lab.session.discover(lab.alice, lab.scope().scope, ScopeTime + 1);
    INFO((discovered.ok() ? std::string{} : discovered.error().diagnostic));
    CHECK(discovered.ok());

    // A failing proposal leaves the live host just as untouched.
    const ProposalId failing = lab.submit(RuntimeErrorSource);
    const LiveSnapshot beforeFailure = snapshot(lab.host);
    CHECK(lab.evaluate(failing).status == EvaluationStatus::Failed);
    require_unchanged(beforeFailure, snapshot(lab.host));
}

TEST_CASE("L2.2 oracle: case factories receive exactly the captured grants, source, inputs and an isolated store") {
    Lab lab;
    auto isolated = std::make_shared<Isolated>();
    EvaluationSuite suite = probe_suite({"grant-probe", 1}, lighting_factory(isolated, &lab.host.world));
    suite.cases[0].feedbackTiming = FeedbackTiming::Immediate;
    lab.register_suite(std::move(suite));
    const ProposalId proposal = lab.submit(ReferenceSource);

    const EvaluationRecord record = lab.evaluate(proposal, {"grant-probe", 1});
    CHECK(record.status == EvaluationStatus::Passed);
    CHECK(isolated->prepareCalls == 2);

    const std::vector<ScopeGrant> expected = lighting_grants();
    REQUIRE(isolated->grantsSeen.size() == expected.size());
    for (const ScopeGrant& grant : expected) {
        const bool present = std::any_of(isolated->grantsSeen.begin(), isolated->grantsSeen.end(),
            [&grant](const ScopeGrant& seen) {
                return seen.scriptTypeName == grant.scriptTypeName
                    && seen.componentName == grant.componentName
                    && seen.mode == grant.mode;
            });
        INFO(grant.scriptTypeName << "." << grant.componentName);
        CHECK(present);
    }
    CHECK(isolated->sourceSeen == ReferenceSource);
    CHECK(isolated->inputsSeen == case_inputs(InitialOffice));
    CHECK(isolated->storeSeen != nullptr);
    CHECK(isolated->storeSeen != &lab.host.store);
    CHECK(isolated->isolatedWorld != &lab.host.world);

    // Final state reports exactly the granted targets, never the Write-only baseline.
    REQUIRE(record.cases.size() == 2);
    const EvaluationCaseResult& first = record.cases.front();
    REQUIRE(first.finalState.size() == expected.size());
    const EvaluationTargetSummary* office = target(first, "office");
    const EvaluationTargetSummary* porch = target(first, "porch");
    const EvaluationTargetSummary* memory = target(first, "memory");
    REQUIRE(office != nullptr);
    REQUIRE(porch != nullptr);
    REQUIRE(memory != nullptr);
    CHECK(office->scriptTypeName == "Light");
    CHECK(office->mode == ComponentAccessMode::ReadWrite);
    CHECK(office->value == std::optional<Value>{level_value(70)});
    CHECK(porch->mode == ComponentAccessMode::Write);
    CHECK_FALSE(porch->value.has_value());
    CHECK(memory->scriptTypeName == "Counter");
    CHECK(memory->value == std::optional<Value>{count_value(2)});
    CHECK(target(first, "hallway") == nullptr);
}

TEST_CASE("L2.2 oracle: no production adapter receives evaluation commands") {
    Lab lab;
    lab.register_lighting();
    const ProposalId proposal = lab.submit(ReferenceSource);

    const EvaluationRecord record = lab.evaluate(proposal);
    REQUIRE(record.cases.size() == LightingCaseCount);
    // The in-memory case adapters were driven ...
    CHECK(record.cases[DeferredCase].frames.at(0).commandsIssued == 1);
    // ... while the live adapter on the same route saw nothing.
    CHECK(lab.host.productionAdapter->pending() == 0);
    CHECK_FALSE(lab.host.productionAdapter->state(EffectTarget{std::string(OfficeDevice)}).has_value());
    CHECK_FALSE(lab.host.runtime.observed_state(AdapterRoute{std::string(LightRoute)}, EffectTarget{std::string(OfficeDevice)}).has_value());
}

TEST_CASE("L2.2 oracle: fixture mismatch against the captured scope is rejected as HostError") {
    struct Mismatch {
        const char* name;
        FactoryVariant variant;
    };
    FactoryVariant extraGrant;
    extraGrant.extraHallwayRead = true;
    FactoryVariant widerSchema;
    widerSchema.lightMaximum = 255;
    FactoryVariant narrowerMode;
    narrowerMode.officeMode = ComponentAccessMode::Read;

    for (const Mismatch& mismatch : {
             Mismatch{"mismatch-extra-grant", extraGrant},
             Mismatch{"mismatch-schema", widerSchema},
             Mismatch{"mismatch-permission", narrowerMode}}) {
        DYNAMIC_SECTION(mismatch.name) {
            Lab lab;
            lab.register_suite(lighting_suite(&lab.host.world, {mismatch.name, 1}, mismatch.variant));
            const ProposalId proposal = lab.submit(ReferenceSource);
            const LiveSnapshot before = snapshot(lab.host);

            const EvaluationRecord record = lab.evaluate(proposal, {mismatch.name, 1});
            CHECK(record.status == EvaluationStatus::HostError);
            REQUIRE(record.cases.size() == LightingCaseCount);
            CHECK(record.cases[0].status == EvaluationCaseStatus::HostError);
            CHECK(record.cases[0].framesRun == 0);
            CHECK(record.cases[0].diagnostic.rfind("fixture mismatch: ", 0) == 0);
            require_not_run_after(record, 0);
            require_unchanged(before, snapshot(lab.host));
        }
    }
}

// =====================================================================
// L2.3 — bounded evaluation loop and records
// =====================================================================

TEST_CASE("L2.3 oracle: the reference proposal passes every lighting-v1 case through the real lifecycle and codec") {
    Lab lab;
    lab.register_lighting();
    const ProposalId proposal = lab.submit(ReferenceSource);

    const EvaluationRecord record = lab.evaluate(proposal);
    CHECK(record.id.valid());
    CHECK(record.session == lab.session.id());
    CHECK(record.proposal == proposal);
    CHECK(record.scope == lab.scope().scope);
    CHECK(record.scopeRevision == lab.scope().revision);
    CHECK(record.fixture == LightingV1);
    CHECK(record.status == EvaluationStatus::Passed);
    CHECK(record.complete);
    REQUIRE(record.cases.size() == LightingCaseCount);
    for (const EvaluationCaseResult& result : record.cases) {
        INFO(result.name << ": " << result.diagnostic);
        CHECK(result.status == EvaluationCaseStatus::Passed);
        CHECK(result.framesRun == result.framesScheduled);
        CHECK(result.frames.size() == result.framesRun);
        for (const EvaluationFrameSummary& frame : result.frames) {
            CHECK(frame.frameCompleted);
            CHECK(frame.lifecycleStatus == std::optional<LuaExecutionStatus>{LuaExecutionStatus::Success});
            CHECK(frame.assertionPassed);
        }
    }
    CHECK(record.cases[PersistentCase].name == "persistent 70 success");
    CHECK(record.cases[ExpiryCase].framesScheduled == 3);

    // Lifecycle/codec evidence: on_start proposes keep, boost and tick.
    const EvaluationCaseResult& persistent = record.cases[PersistentCase];
    REQUIRE(persistent.frames.size() == 3);
    CHECK(persistent.frames[0].now == 0);
    CHECK(persistent.frames[0].createdIntents == 3);
    CHECK(persistent.frames[0].liveCandidateIntents == 3);
    CHECK(persistent.frames[0].candidateSelected);
    CHECK(persistent.frames[0].commandsIssued == 0);
    CHECK(persistent.frames[1].now == 10);
    CHECK(persistent.frames[1].createdIntents == 1);
    CHECK(persistent.frames[1].cancelledIntents == 1);
    CHECK(persistent.frames[2].now == 20);
    const EvaluationTargetSummary* office = target(persistent, "office");
    REQUIRE(office != nullptr);
    CHECK(office->value == std::optional<Value>{level_value(70)});
}

TEST_CASE("lighting-v1: competing higher-priority 0 then reselection of the original 70 hides competitor details") {
    Lab lab;
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource));
    REQUIRE(record.cases.size() == LightingCaseCount);
    const EvaluationCaseResult& competitor = record.cases[CompetitorCase];
    INFO(competitor.diagnostic);
    CHECK(competitor.status == EvaluationCaseStatus::Passed);
    REQUIRE(competitor.frames.size() == 3);
    // Only the candidate's own intents are counted: keep + tick once boost expired.
    CHECK(competitor.frames[1].liveCandidateIntents == 2);
    CHECK(competitor.frames[2].liveCandidateIntents == 2);
    // The competitor's slot is not a granted target, so nothing of it is reported.
    CHECK(competitor.finalState.size() == lighting_grants().size());
    const EvaluationTargetSummary* office = target(competitor, "office");
    REQUIRE(office != nullptr);
    CHECK(office->value == std::optional<Value>{level_value(70)});
}

TEST_CASE("lighting-v1: until-time expiration at the declared deadline and the frame right after it") {
    Lab lab;
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource));
    REQUIRE(record.cases.size() == LightingCaseCount);
    const EvaluationCaseResult& expiry = record.cases[ExpiryCase];
    INFO(expiry.diagnostic);
    CHECK(expiry.status == EvaluationCaseStatus::Passed);
    REQUIRE(expiry.frames.size() == 3);
    CHECK(expiry.frames[1].now == static_cast<IntentTime>(ExpiryDeadline - 1));
    CHECK(expiry.frames[1].liveCandidateIntents == 3);
    CHECK(expiry.frames[2].now == static_cast<IntentTime>(ExpiryDeadline + 1));
    CHECK(expiry.frames[2].liveCandidateIntents == 2);
}

TEST_CASE("lighting-v1: internal-state persistence across fresh VMs") {
    Lab lab;
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource));
    REQUIRE(record.cases.size() == LightingCaseCount);
    const EvaluationCaseResult& state = record.cases[InternalStateCase];
    INFO(state.diagnostic);
    CHECK(state.status == EvaluationCaseStatus::Passed);
    const EvaluationTargetSummary* memory = target(state, "memory");
    REQUIRE(memory != nullptr);
    CHECK(memory->value == std::optional<Value>{count_value(3)});
}

TEST_CASE("lighting-v1: deferred feedback projects the Applied report one frame later") {
    Lab lab;
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource));
    REQUIRE(record.cases.size() == LightingCaseCount);
    const EvaluationCaseResult& deferred = record.cases[DeferredCase];
    INFO(deferred.diagnostic);
    CHECK(deferred.status == EvaluationCaseStatus::Passed);
    REQUIRE(deferred.frames.size() == 3);
    CHECK(deferred.frames[0].commandsIssued == 1);
    CHECK(deferred.frames[0].reportStatuses.empty());
    CHECK(deferred.frames[1].reportStatuses == std::vector<CommandStatus>{CommandStatus::Applied});
}

TEST_CASE("lighting-v1: rejected, silent, duplicate and delayed reports") {
    Lab lab;
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource));
    REQUIRE(record.cases.size() == LightingCaseCount);

    const EvaluationCaseResult& rejected = record.cases[RejectedCase];
    INFO(rejected.diagnostic);
    CHECK(rejected.status == EvaluationCaseStatus::Passed);
    REQUIRE(rejected.frames.size() == 3);
    CHECK(rejected.frames[1].reportStatuses == std::vector<CommandStatus>{CommandStatus::Rejected});
    CHECK(target(rejected, "office")->value == std::optional<Value>{level_value(InitialOffice)});

    const EvaluationCaseResult& silent = record.cases[SilentCase];
    INFO(silent.diagnostic);
    CHECK(silent.status == EvaluationCaseStatus::Passed);
    for (const EvaluationFrameSummary& frame : silent.frames)
        CHECK(frame.reportStatuses.empty());

    const EvaluationCaseResult& duplicate = record.cases[DuplicateCase];
    INFO(duplicate.diagnostic);
    CHECK(duplicate.status == EvaluationCaseStatus::Passed);
    REQUIRE(duplicate.frames.size() == 3);
    CHECK(duplicate.frames[1].reportStatuses == std::vector<CommandStatus>{CommandStatus::Applied});

    const EvaluationCaseResult& delayed = record.cases[DelayedCase];
    INFO(delayed.diagnostic);
    CHECK(delayed.status == EvaluationCaseStatus::Passed);
    REQUIRE(delayed.frames.size() == 3);
    CHECK(delayed.frames[1].reportStatuses.empty());
    CHECK(delayed.frames[2].reportStatuses == std::vector<CommandStatus>{CommandStatus::Applied});
}

TEST_CASE("lighting-v1: no model connectivity beyond the case's in-memory adapter") {
    Lab lab;
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource));
    REQUIRE(record.cases.size() == LightingCaseCount);
    const EvaluationCaseResult& connectivity = record.cases[ConnectivityCase];
    INFO(connectivity.diagnostic);
    CHECK(connectivity.status == EvaluationCaseStatus::Passed);
    CHECK(lab.host.productionAdapter->pending() == 0);
}

TEST_CASE("negative proposal: syntax error fails the first case and later cases are NotRun") {
    Lab lab;
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(SyntaxErrorSource));
    require_first_case_stopped(record, EvaluationStatus::Failed, EvaluationCaseStatus::Failed, 1);
    CHECK(last_lifecycle(record) == std::optional<LuaExecutionStatus>{LuaExecutionStatus::SyntaxError});
}

TEST_CASE("negative proposal: runtime error is Failed") {
    Lab lab;
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(RuntimeErrorSource));
    require_first_case_stopped(record, EvaluationStatus::Failed, EvaluationCaseStatus::Failed, 1);
    CHECK(last_lifecycle(record) == std::optional<LuaExecutionStatus>{LuaExecutionStatus::RuntimeError});
}

TEST_CASE("negative proposal: forbidden access to an ungranted component is Failed without intents") {
    Lab lab;
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(ForbiddenAccessSource));
    require_first_case_stopped(record, EvaluationStatus::Failed, EvaluationCaseStatus::Failed, 1);
    const std::optional<LuaExecutionStatus> status = last_lifecycle(record);
    CHECK((status == LuaExecutionStatus::RuntimeError || status == LuaExecutionStatus::InvalidProposal));
    REQUIRE_FALSE(record.cases.front().frames.empty());
    CHECK(record.cases.front().frames.front().liveCandidateIntents == 0);
    CHECK(record.cases.front().frames.front().createdIntents == 0);
}

TEST_CASE("negative proposal: invalid codec shape is InvalidProposal") {
    Lab lab;
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(InvalidCodecSource));
    require_first_case_stopped(record, EvaluationStatus::Failed, EvaluationCaseStatus::Failed, 1);
    CHECK(last_lifecycle(record) == std::optional<LuaExecutionStatus>{LuaExecutionStatus::InvalidProposal});
    CHECK(record.cases.front().frames.front().liveCandidateIntents == 0);
}

TEST_CASE("negative proposal: failed cancellation/replacement bundle rolls back to the exact intent set") {
    Lab lab;
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(FailedBundleSource));
    require_first_case_stopped(record, EvaluationStatus::Failed, EvaluationCaseStatus::Failed, 2);
    const EvaluationCaseResult& first = record.cases.front();
    REQUIRE(first.frames.size() == 2);
    CHECK(first.frames[0].lifecycleStatus == std::optional<LuaExecutionStatus>{LuaExecutionStatus::Success});
    CHECK(first.frames[1].lifecycleStatus == std::optional<LuaExecutionStatus>{LuaExecutionStatus::CommitFailed});
    CHECK(first.frames[1].createdIntents == 0);
    CHECK(first.frames[1].cancelledIntents == 0);
    CHECK(first.frames[1].liveCandidateIntents == first.frames[0].liveCandidateIntents);
    const EvaluationTargetSummary* office = target(first, "office");
    REQUIRE(office != nullptr);
    CHECK(office->value == std::optional<Value>{level_value(70)});
}

TEST_CASE("negative proposal: instruction limit is LimitExceeded") {
    Lab lab;
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(InstructionLimitSource));
    require_first_case_stopped(record, EvaluationStatus::LimitExceeded, EvaluationCaseStatus::LimitExceeded, 1);
    CHECK(last_lifecycle(record) == std::optional<LuaExecutionStatus>{LuaExecutionStatus::InstructionLimitExceeded});
}

TEST_CASE("negative proposal: memory limit is LimitExceeded") {
    Lab lab;
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(MemoryLimitSource));
    require_first_case_stopped(record, EvaluationStatus::LimitExceeded, EvaluationCaseStatus::LimitExceeded, 1);
    CHECK(last_lifecycle(record) == std::optional<LuaExecutionStatus>{LuaExecutionStatus::MemoryLimitExceeded});
}

TEST_CASE("L2.3 oracle: a failed host assertion is Failed with its diagnostic and later cases NotRun") {
    Lab lab;
    auto isolated = std::make_shared<Isolated>();
    lab.register_suite(probe_suite({"assert-probe", 1}, lighting_factory(isolated, &lab.host.world), {},
        [](const PreparedEvaluation&, const EvaluationFrameContext& context) {
            return EvaluationAssertion{context.frameIndex == 0, "office was expected to dim"};
        }));
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource), {"assert-probe", 1});
    CHECK(record.status == EvaluationStatus::Failed);
    REQUIRE(record.cases.size() == 2);
    CHECK(record.cases[0].status == EvaluationCaseStatus::Failed);
    CHECK(record.cases[0].framesRun == 2);
    REQUIRE(record.cases[0].frames.size() == 2);
    CHECK(record.cases[0].frames[0].assertionPassed);
    CHECK_FALSE(record.cases[0].frames[1].assertionPassed);
    CHECK(record.cases[0].diagnostic.find("office was expected to dim") != std::string::npos);
    require_not_run_after(record, 0);
}

TEST_CASE("L2.3 oracle: host callback exceptions are contained as HostError") {
    Lab lab;
    const World* live = &lab.host.world;
    auto isolated = std::make_shared<Isolated>();
    FactoryVariant throwing;
    throwing.throwOnPrepare = true;
    lab.register_suite(probe_suite({"factory-throws", 1}, lighting_factory(isolated, live, throwing)));
    lab.register_suite(probe_suite({"input-throws", 1}, lighting_factory(isolated, live),
        [](PreparedEvaluation&, const EvaluationFrameContext& context) {
            if (context.frameIndex == 1)
                throw std::runtime_error("host input failed");
        }));
    lab.register_suite(probe_suite({"expect-throws", 1}, lighting_factory(isolated, live), {},
        [](const PreparedEvaluation&, const EvaluationFrameContext&) -> EvaluationAssertion {
            throw std::logic_error("host expectation failed");
        }));
    const ProposalId proposal = lab.submit(ReferenceSource);
    const LiveSnapshot before = snapshot(lab.host);

    struct Expected {
        const char* fixture;
        std::size_t framesRun;
    };
    for (const Expected& expected : {Expected{"factory-throws", 0}, Expected{"input-throws", 1}, Expected{"expect-throws", 1}}) {
        INFO(expected.fixture);
        const EvaluationRecord record = lab.evaluate(proposal, {expected.fixture, 1});
        CHECK(record.status == EvaluationStatus::HostError);
        REQUIRE(record.cases.size() == 2);
        CHECK(record.cases[0].status == EvaluationCaseStatus::HostError);
        CHECK(record.cases[0].framesRun == expected.framesRun);
        CHECK_FALSE(record.cases[0].diagnostic.empty());
        require_not_run_after(record, 0);
    }
    require_unchanged(before, snapshot(lab.host));
}

TEST_CASE("L2.3 oracle: a callback that drives run_frame itself is HostError") {
    Lab lab;
    auto isolated = std::make_shared<Isolated>();
    lab.register_suite(probe_suite({"input-drives-frames", 1}, lighting_factory(isolated, &lab.host.world),
        [](PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
            FrameInput sneaky;
            sneaky.now = context.now;
            prepared.runtime->run_frame(std::move(sneaky));
        }));
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource), {"input-drives-frames", 1});
    CHECK(record.status == EvaluationStatus::HostError);
    REQUIRE(record.cases.size() == 2);
    CHECK(record.cases[0].status == EvaluationCaseStatus::HostError);
    require_not_run_after(record, 0);
}

TEST_CASE("L2.3 oracle: a SingleReadable candidate without a script grant has no lifecycle result and is HostError") {
    Lab lab;
    auto isolated = std::make_shared<Isolated>();
    FactoryVariant noScript;
    noScript.omitScriptGrant = true;
    lab.register_suite(probe_suite({"no-lifecycle", 1}, lighting_factory(isolated, &lab.host.world, noScript)));
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource), {"no-lifecycle", 1});
    CHECK(record.status == EvaluationStatus::HostError);
    REQUIRE(record.cases.size() == 2);
    CHECK(record.cases[0].status == EvaluationCaseStatus::HostError);
    CHECK(record.cases[0].framesRun == 1);
    CHECK(record.cases[0].diagnostic.find("no lifecycle result for the candidate") != std::string::npos);
    require_not_run_after(record, 0);
}

TEST_CASE("L2.3 oracle: event-store record budget exhaustion ends the evaluation as LimitExceeded") {
    AuthoringLimits limits;
    limits.maxEvaluationRecords = 4;
    Lab lab(limits);
    lab.register_lighting();
    const ProposalId proposal = lab.submit(ReferenceSource);
    const LiveSnapshot before = snapshot(lab.host);

    const EvaluationRecord record = lab.evaluate(proposal);
    CHECK(record.status == EvaluationStatus::LimitExceeded);
    REQUIRE_FALSE(record.cases.empty());
    CHECK(record.cases.front().status == EvaluationCaseStatus::LimitExceeded);
    require_not_run_after(record, 0);
    require_unchanged(before, snapshot(lab.host));
}

TEST_CASE("L2.3 oracle: event-store byte budget exhaustion ends the evaluation as LimitExceeded") {
    AuthoringLimits limits;
    limits.maxEvaluationRecordBytes = 256;
    Lab lab(limits);
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource));
    CHECK(record.status == EvaluationStatus::LimitExceeded);
    REQUIRE_FALSE(record.cases.empty());
    CHECK(record.cases.front().status == EvaluationCaseStatus::LimitExceeded);
    require_not_run_after(record, 0);
}

TEST_CASE("L2.3 oracle: a response over maxEvaluationResponseBytes is LimitExceeded and incomplete") {
    AuthoringLimits limits;
    limits.maxEvaluationResponseBytes = 64;
    Lab lab(limits);
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource));
    CHECK(record.status == EvaluationStatus::LimitExceeded);
    CHECK_FALSE(record.complete);
}

TEST_CASE("L2.3 oracle: maxEvaluations bounds admitted evaluations per session") {
    AuthoringLimits limits;
    limits.maxEvaluations = 1;
    Lab lab(limits);
    lab.register_lighting();
    const ProposalId proposal = lab.submit(ReferenceSource);
    CHECK(lab.evaluate(proposal).id == EvaluationId{1});
    require_error(lab.session.evaluate(lab.alice, proposal, LightingV1), AuthoringErrorCode::LimitExceeded);
    require_error(lab.session.normalized_trace(EvaluationId{2}), AuthoringErrorCode::NotFound);
}

TEST_CASE("L2.3 oracle: the session payload reservation is checked before admission") {
    AuthoringLimits limits;
    limits.maxSessionPayloadBytes = limits.maxEvaluationRecordBytes;
    Lab lab(limits);
    lab.register_lighting();
    const ProposalId proposal = lab.submit(ReferenceSource);
    const LiveSnapshot before = snapshot(lab.host);
    require_error(lab.session.evaluate(lab.alice, proposal, LightingV1), AuthoringErrorCode::LimitExceeded);
    require_error(lab.session.normalized_trace(EvaluationId{1}), AuthoringErrorCode::NotFound);
    require_unchanged(before, snapshot(lab.host));
}

TEST_CASE("L2.3 oracle: stale or revoked scopes block evaluation without a record") {
    SECTION("replaced scope") {
        Lab lab;
        lab.register_lighting();
        const ProposalId proposal = lab.submit(ReferenceSource);
        REQUIRE(lab.session.replace_scope(lab.scope().scope, lab.scope().revision, lighting_grants(), ScopeTime).ok());
        require_error(lab.session.evaluate(lab.alice, proposal, LightingV1), AuthoringErrorCode::StaleScope);
        require_error(lab.session.normalized_trace(EvaluationId{1}), AuthoringErrorCode::NotFound);
    }
    SECTION("revoked scope") {
        Lab lab;
        lab.register_lighting();
        const ProposalId proposal = lab.submit(ReferenceSource);
        REQUIRE(lab.session.revoke_scope(lab.scope().scope, lab.scope().revision).ok());
        require_error(lab.session.evaluate(lab.alice, proposal, LightingV1), AuthoringErrorCode::StaleScope);
        require_error(lab.session.normalized_trace(EvaluationId{1}), AuthoringErrorCode::NotFound);
    }
}

TEST_CASE("L2.3 oracle: evaluation IDs advance only on admission") {
    Lab lab;
    lab.register_lighting();
    const ProposalId proposal = lab.submit(ReferenceSource);
    require_error(lab.session.evaluate(lab.alice, proposal, {"missing", 1}), AuthoringErrorCode::NotFound);
    CHECK(lab.evaluate(proposal).id == EvaluationId{1});
    require_error(lab.session.evaluate(CallerContext{lab.session.id(), "bob"}, proposal, LightingV1), AuthoringErrorCode::NotFound);
    CHECK(lab.evaluate(lab.submit(RuntimeErrorSource)).id == EvaluationId{2});
}

TEST_CASE("L2.3 normalized_trace: populated for an admitted evaluation, NotFound otherwise") {
    Lab lab;
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource));

    const auto trace = lab.session.normalized_trace(record.id);
    INFO((trace.ok() ? std::string{} : trace.error().diagnostic));
    REQUIRE(trace.ok());
    CHECK(trace.value().evaluation == record.id);
    REQUIRE_FALSE(trace.value().entries.empty());
    for (const EvaluationTraceEntry& entry : trace.value().entries)
        CHECK(entry.caseIndex < record.cases.size());
    CHECK(std::any_of(trace.value().entries.begin(), trace.value().entries.end(),
        [](const EvaluationTraceEntry& entry) { return entry.type == EventType::ScriptExecuted; }));

    require_error(lab.session.normalized_trace(EvaluationId{record.id.value() + 1}), AuthoringErrorCode::NotFound);
    require_error(lab.session.normalized_trace(EvaluationId{}), AuthoringErrorCode::NotFound);
}

TEST_CASE("L2.3 oracle: a kept evaluation releases the unused part of its reservation") {
    AuthoringLimits limits;
    // The lighting-v1 reservation: a 2-field entry, the record header (11
    // fields, 7 scalars, 1 flag and the fixture-name bytes), the response
    // limit and the run budget.
    const std::size_t header = 11 * 8 + 7 * 8 + 1 + LightingV1.stableName.size();
    const std::size_t reservation =
        2 * 8 + header + limits.maxEvaluationResponseBytes + limits.maxEvaluationRecordBytes;
    // Two reservations never fit at once; one reservation beside the much
    // smaller kept charge of the first evaluation does.
    limits.maxSessionPayloadBytes = 2 * reservation - 1;
    Lab lab(limits);
    lab.register_lighting();
    const ProposalId proposal = lab.submit(ReferenceSource);
    CHECK(lab.evaluate(proposal).id == EvaluationId{1});
    CHECK(lab.evaluate(proposal).id == EvaluationId{2});
}

// Spec-concern repro (review r1): a host competitor writes a target outside the
// candidate's scope through the case adapter; the frame summary should count
// only commands and reports on granted targets.
TEST_CASE("L2 b14: frame summaries count only the candidate's granted targets", "[l2-b14]") {
    Lab lab;
    auto isolated = std::make_shared<Isolated>();
    const EvaluationCaseFactory base = lighting_factory(isolated, &lab.host.world);
    const EvaluationCaseFactory factory = [isolated, base](const EvaluationPreparation& prep) {
        PreparedEvaluation prepared = base(prep);
        World& world = prepared.runtime->world();
        prepared.runtime->bind_effect_component(isolated->light, "hallway", EffectTarget{"hallway-device"});
        const BehaviorId competitor = world.create_behavior();
        world.grant_component_access(isolated->light, competitor, "hallway", ComponentAccessMode::ReadWrite);
        world.create_intent(competitor, isolated->light, world.get_components(isolated->light, competitor).at("hallway"),
            IntentLifetime::persistent(), Light{90}, IntentPriority::High, "hallway");
        return prepared;
    };
    const EvaluationFixtureId fixture{"repro-scoped-summary", 1};
    EvaluationSuite suite = probe_suite(fixture, factory);
    for (EvaluationCase& evaluationCase : suite.cases)
        evaluationCase.adapters = {AdapterBehavior{}};
    lab.register_suite(std::move(suite));

    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource), fixture);
    REQUIRE_FALSE(record.cases.empty());
    const EvaluationCaseResult& first = record.cases.front();
    INFO("case status " << static_cast<int>(first.status) << ": " << first.diagnostic);
    REQUIRE(first.frames.size() == 2);
    INFO("frame 0 commandsIssued " << first.frames[0].commandsIssued
        << ", frame 0 reports " << first.frames[0].reportStatuses.size()
        << ", frame 1 reports " << first.frames[1].reportStatuses.size());
    // Only the granted office target belongs to the candidate's summary.
    REQUIRE(first.frames[0].commandsIssued == 1);
    REQUIRE(first.frames[0].reportStatuses.size() + first.frames[1].reportStatuses.size() == 1);
}

// =====================================================================
// L2.4 — repeatability, input sensitivity and isolation
// =====================================================================

namespace {

// Every public field except the per-session identity (id, session, proposal).
void require_same_evaluation(const EvaluationRecord& first, const EvaluationRecord& second) {
    CHECK(second.scope == first.scope);
    CHECK(second.scopeRevision == first.scopeRevision);
    CHECK(second.fixture == first.fixture);
    CHECK(second.status == first.status);
    CHECK(second.complete == first.complete);
    REQUIRE(second.cases.size() == first.cases.size());
    for (std::size_t index = 0; index < first.cases.size(); ++index) {
        const EvaluationCaseResult& expected = first.cases[index];
        const EvaluationCaseResult& actual = second.cases[index];
        INFO("case " << index << ": " << expected.name);
        CHECK(actual.name == expected.name);
        CHECK(actual.status == expected.status);
        CHECK(actual.framesScheduled == expected.framesScheduled);
        CHECK(actual.framesRun == expected.framesRun);
        CHECK(actual.diagnostic == expected.diagnostic);
        CHECK(actual.frames == expected.frames);
        CHECK(actual.finalState == expected.finalState);
    }
}

EvaluationTrace trace_of(const AuthoringSession& session, EvaluationId evaluation) {
    const auto trace = session.normalized_trace(evaluation);
    INFO((trace.ok() ? std::string{} : trace.error().diagnostic));
    REQUIRE(trace.ok());
    return trace.value();
}

std::vector<EvaluationTraceEntry> case_entries(const EvaluationTrace& trace, std::size_t caseIndex) {
    std::vector<EvaluationTraceEntry> entries;
    for (const EvaluationTraceEntry& entry : trace.entries)
        if (entry.caseIndex == caseIndex)
            entries.push_back(entry);
    return entries;
}

// Copies the frozen office level into Counter state, so the outcome follows
// the case input.
const std::string EchoInputSource = R"lua(
function on_start(frame)
    access.Counter.memory.propose{
        name = "echo",
        value = {count = access.Light.office.value.level},
        priority = "medium",
        lifetime = "persistent"
    }
end
)lua";

const EvaluationFixtureId EchoInput{"echo-input", 1};

// Two cases that differ only in the frozen initial office level.
EvaluationSuite echo_suite(const World* liveWorld) {
    EvaluationSuite suite;
    suite.id = EchoInput;
    for (const std::int64_t level : {std::int64_t{10}, std::int64_t{25}}) {
        auto isolated = std::make_shared<Isolated>();
        EvaluationCase evaluationCase = lighting_case("echo " + std::to_string(level), isolated, liveWorld, {});
        evaluationCase.inputs = case_inputs(level);
        evaluationCase.frameTimes = {0, 10};
        evaluationCase.expect = [isolated](const PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
            Verdict verdict = base_verdict(context);
            const std::int64_t expected = context.inputs ? input_integer(*context.inputs, "initialLevel", -1) : -1;
            verdict.require(level_of(prepared, *isolated) == expected, "office is not the input level")
                .require(count_of(prepared, *isolated) == expected, "memory does not echo the input level");
            return verdict.done();
        };
        suite.cases.push_back(std::move(evaluationCase));
    }
    return suite;
}

// An independent behavior with its own script slot; it may touch only the
// hallway, which the candidate cannot.
const std::string NeighborSource = R"lua(
function on_start(frame)
    access.Light.hallway.propose{name = "neighbor", value = {level = 55}, priority = "low", lifetime = "persistent"}
end
)lua";

const EvaluationFixtureId TwoScripts{"two-scripts", 1};

struct Neighbor {
    BehaviorId behavior{};
    std::size_t succeededFrames = 0;
};

EvaluationCaseFactory two_script_factory(
    std::shared_ptr<Isolated> isolated,
    std::shared_ptr<Neighbor> neighbor,
    const World* liveWorld
) {
    const EvaluationCaseFactory base = lighting_factory(isolated, liveWorld);
    return [isolated, neighbor, base](const EvaluationPreparation& prep) {
        PreparedEvaluation prepared = base(prep);
        World& world = prepared.runtime->world();
        world.add_component(prepared.scriptType, "neighbor", LuaBehaviorScript{NeighborSource, 1});
        neighbor->behavior = world.create_behavior();
        world.grant_component_access(isolated->light, neighbor->behavior, "hallway", ComponentAccessMode::ReadWrite);
        world.grant_component_access(prepared.scriptType, neighbor->behavior, "neighbor", ComponentAccessMode::Read);
        prepared.runtime->configure_component(isolated->light, "hallway", ComponentControl::InternalState);
        return prepared;
    };
}

EvaluationExpectation two_script_expectation(std::shared_ptr<Isolated> isolated, std::shared_ptr<Neighbor> neighbor) {
    return [isolated, neighbor](const PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
        World& world = prepared.runtime->world();
        const auto candidateScripts = world.get_components(prepared.scriptType, prepared.candidate);
        const auto neighborScripts = world.get_components(prepared.scriptType, neighbor->behavior);
        const LuaExecutionResult* neighborResult =
            world.get_system<LuaLifecycleSystem>().last_result(neighbor->behavior);
        const bool neighborRan = neighborResult != nullptr && neighborResult->succeeded();
        if (neighborRan)
            ++neighbor->succeededFrames;
        Verdict verdict = base_verdict(context);
        verdict.require(candidateScripts.size() == 1 && candidateScripts.contains("lifecycle"),
                "candidate does not read exactly the lifecycle slot")
            .require(neighborScripts.size() == 1 && neighborScripts.contains("neighbor"),
                "neighbor does not read exactly the neighbor slot")
            .require(neighborRan, "neighbor lifecycle did not run")
            .require(level_of(prepared, *isolated) == 70, "candidate source did not set office")
            .require(level_of(prepared, *isolated, "hallway") == 55, "neighbor source did not set hallway");
        return verdict.done();
    };
}

}

TEST_CASE("L2.4: two fresh evaluations of one proposal and fixture are equal", "[l2.4]") {
    Lab first;
    first.register_lighting();
    const ProposalId proposal = first.submit(ReferenceSource);
    const EvaluationRecord original = first.evaluate(proposal);
    const EvaluationRecord repeated = first.evaluate(proposal);

    Lab second;
    second.register_lighting();
    const EvaluationRecord fresh = second.evaluate(second.submit(ReferenceSource));

    // A non-trivial baseline, so equality is not vacuous.
    REQUIRE(original.status == EvaluationStatus::Passed);
    REQUIRE(original.complete);
    REQUIRE(original.cases.size() == LightingCaseCount);
    CHECK(repeated.id != original.id);
    require_same_evaluation(original, repeated);
    require_same_evaluation(original, fresh);

    const EvaluationTrace originalTrace = trace_of(first.session, original.id);
    REQUIRE_FALSE(originalTrace.entries.empty());
    CHECK(std::any_of(originalTrace.entries.begin(), originalTrace.entries.end(),
        [](const EvaluationTraceEntry& entry) { return entry.type == EventType::ScriptExecuted; }));
    CHECK(trace_of(first.session, repeated.id).entries == originalTrace.entries);
    CHECK(trace_of(second.session, fresh.id).entries == originalTrace.entries);
}

TEST_CASE("L2.4: a different frozen input gives the different expected outcome", "[l2.4]") {
    Lab lab;
    lab.register_suite(echo_suite(&lab.host.world));
    const EvaluationRecord record = lab.evaluate(lab.submit(EchoInputSource), EchoInput);
    CHECK(record.status == EvaluationStatus::Passed);
    REQUIRE(record.cases.size() == 2);

    for (std::size_t index = 0; index < record.cases.size(); ++index) {
        INFO("case " << index << ": " << record.cases[index].diagnostic);
        CHECK(record.cases[index].status == EvaluationCaseStatus::Passed);
    }
    const EvaluationTargetSummary* lowOffice = target(record.cases[0], "office");
    const EvaluationTargetSummary* lowMemory = target(record.cases[0], "memory");
    const EvaluationTargetSummary* highOffice = target(record.cases[1], "office");
    const EvaluationTargetSummary* highMemory = target(record.cases[1], "memory");
    REQUIRE(lowOffice != nullptr);
    REQUIRE(lowMemory != nullptr);
    REQUIRE(highOffice != nullptr);
    REQUIRE(highMemory != nullptr);
    CHECK(lowOffice->value == std::optional<Value>{level_value(10)});
    CHECK(lowMemory->value == std::optional<Value>{count_value(10)});
    CHECK(highOffice->value == std::optional<Value>{level_value(25)});
    CHECK(highMemory->value == std::optional<Value>{count_value(25)});

    // The same source and frames leave different evidence for each input.
    const EvaluationTrace trace = trace_of(lab.session, record.id);
    const std::vector<EvaluationTraceEntry> low = case_entries(trace, 0);
    const std::vector<EvaluationTraceEntry> high = case_entries(trace, 1);
    REQUIRE_FALSE(low.empty());
    CHECK(low.size() == high.size());
    CHECK(low != high);
}

TEST_CASE("L2.4: two behaviors with different script slots each run their own source", "[l2.4]") {
    Lab lab;
    auto isolated = std::make_shared<Isolated>();
    auto neighbor = std::make_shared<Neighbor>();
    lab.register_suite(probe_suite(TwoScripts,
        two_script_factory(isolated, neighbor, &lab.host.world), {}, two_script_expectation(isolated, neighbor)));

    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource), TwoScripts);
    REQUIRE(record.cases.size() == 2);
    INFO("first case: " << record.cases[0].diagnostic);
    CHECK(record.status == EvaluationStatus::Passed);
    for (const EvaluationCaseResult& result : record.cases) {
        CHECK(result.status == EvaluationCaseStatus::Passed);
        CHECK(result.framesRun == 2);
    }
    // Both cases, both frames.
    CHECK(neighbor->succeededFrames == 4);
    const EvaluationTargetSummary* office = target(record.cases[0], "office");
    REQUIRE(office != nullptr);
    CHECK(office->value == std::optional<Value>{level_value(70)});
    CHECK(target(record.cases[0], "hallway") == nullptr);
}

TEST_CASE("L2.4: evaluations leave the observable live state exactly as before", "[l2.4]") {
    Lab lab;
    auto isolated = std::make_shared<Isolated>();
    auto neighbor = std::make_shared<Neighbor>();
    lab.register_lighting();
    lab.register_suite(echo_suite(&lab.host.world));
    lab.register_suite(probe_suite(TwoScripts,
        two_script_factory(isolated, neighbor, &lab.host.world), {}, two_script_expectation(isolated, neighbor)));
    const ProposalId reference = lab.submit(ReferenceSource);
    const ProposalId echo = lab.submit(EchoInputSource);
    const LiveSnapshot before = snapshot(lab.host);

    CHECK(lab.evaluate(reference).status == EvaluationStatus::Passed);
    CHECK(lab.evaluate(echo, EchoInput).status == EvaluationStatus::Passed);
    CHECK(lab.evaluate(reference, TwoScripts).status == EvaluationStatus::Passed);
    require_unchanged(before, snapshot(lab.host));

    // The isolated worlds were real, separate worlds that did run Lua.
    CHECK(neighbor->succeededFrames == 4);
    CHECK(isolated->isolatedWorld != &lab.host.world);
}

// =====================================================================
// L2 review c1 — both selection modes through the evaluator
// =====================================================================

namespace {

enum class Selection {
    Legacy,
    SingleReadable
};

// A lighting-v1 world whose lifecycle system uses `selection`; an empty
// `scriptMode` leaves the candidate without a script grant.
EvaluationCaseFactory selection_factory(
    std::shared_ptr<Isolated> isolated,
    Selection selection,
    std::optional<ComponentAccessMode> scriptMode = ComponentAccessMode::Read
) {
    return [isolated, selection, scriptMode](const EvaluationPreparation& prep) {
        PreparedEvaluation prepared;
        prepared.runtime = std::make_unique<Runtime>(prep.runtimeOptions);
        World& world = prepared.runtime->world();
        const LightingTypes types = register_lighting_types(world);
        world.add_component(types.light, "office", Light{input_integer(prep.inputs, "initialLevel", InitialOffice)});
        world.add_component(types.light, "porch", Light{33});
        world.add_component(types.counter, "memory", Counter{0});
        world.add_component(types.script, "lifecycle", LuaBehaviorScript{prep.source, 1});

        prepared.candidate = world.create_behavior();
        for (const ScopeGrant& grant : prep.grants)
            grant_by_name(world, types, prepared.candidate, grant);
        if (scriptMode)
            world.grant_component_access(types.script, prepared.candidate, "lifecycle", *scriptMode);

        prepared.runner = std::make_shared<LuaBehaviorRunner>();
        expose_lighting(*prepared.runner, types);
        Signature signature;
        signature.set(types.script.id);
        if (selection == Selection::Legacy)
            world.register_system<LuaLifecycleSystem>(
                signature, SystemPhase::Behavior, types.script, prepared.runner, "lifecycle");
        else
            world.register_system<LuaLifecycleSystem>(
                signature, SystemPhase::Behavior, types.script, prepared.runner, LuaScriptSelection::SingleReadable);
        prepared.runtime->configure_component(types.counter, "memory", ComponentControl::InternalState);
        wire_adapters(prepared, types, prep);
        prepared.scriptType = types.script;

        isolated->light = types.light;
        isolated->counter = types.counter;
        isolated->candidate = prepared.candidate;
        return prepared;
    };
}

bool any_script_executed(const EvaluationTrace& trace) {
    return std::any_of(trace.entries.begin(), trace.entries.end(),
        [](const EvaluationTraceEntry& entry) { return entry.type == EventType::ScriptExecuted; });
}

void require_stopped_with(const EvaluationRecord& record, std::string_view diagnostic) {
    CHECK(record.status == EvaluationStatus::HostError);
    REQUIRE(record.cases.size() == 2);
    INFO("case 0: " << record.cases[0].diagnostic);
    CHECK(record.cases[0].status == EvaluationCaseStatus::HostError);
    CHECK(record.cases[0].framesRun == 1);
    CHECK(record.cases[0].diagnostic.find(diagnostic) != std::string::npos);
    require_not_run_after(record, 0);
}

constexpr std::string_view NoLifecycleResult = "no lifecycle result for the candidate";

}

TEST_CASE("L2 c1: the legacy selection evaluates the reference proposal as Passed", "[l2.4][l2-c1]") {
    Lab lab;
    auto isolated = std::make_shared<Isolated>();
    const EvaluationFixtureId fixture{"legacy-selection", 1};
    lab.register_suite(probe_suite(fixture, selection_factory(isolated, Selection::Legacy), {},
        [isolated](const PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
            Verdict verdict = base_verdict(context);
            verdict.require(level_of(prepared, *isolated) == 70, "office is not 70")
                .require(recorded(context, EventType::ScriptExecuted), "no ScriptExecuted evidence");
            return verdict.done();
        }));

    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource), fixture);
    REQUIRE(record.cases.size() == 2);
    INFO("case 0: " << record.cases[0].diagnostic);
    CHECK(record.status == EvaluationStatus::Passed);
    for (const EvaluationCaseResult& result : record.cases) {
        CHECK(result.status == EvaluationCaseStatus::Passed);
        CHECK(result.framesRun == 2);
    }
    const EvaluationTargetSummary* office = target(record.cases[0], "office");
    REQUIRE(office != nullptr);
    CHECK(office->value == std::optional<Value>{level_value(70)});
    CHECK(any_script_executed(trace_of(lab.session, record.id)));
}

TEST_CASE("L2 c1: a legacy selection without a readable candidate script is HostError", "[l2.4][l2-c1]") {
    // Granted but unreadable, and not granted at all.
    for (const std::optional<ComponentAccessMode> scriptMode :
             {std::optional<ComponentAccessMode>{ComponentAccessMode::Write}, std::optional<ComponentAccessMode>{}}) {
        DYNAMIC_SECTION("script grant " << (scriptMode ? "Write" : "none")) {
            Lab lab;
            auto isolated = std::make_shared<Isolated>();
            const EvaluationFixtureId fixture{"legacy-no-script", 1};
            lab.register_suite(probe_suite(fixture, selection_factory(isolated, Selection::Legacy, scriptMode)));
            require_stopped_with(lab.evaluate(lab.submit(ReferenceSource), fixture), NoLifecycleResult);
        }
    }
}

TEST_CASE("L2 c1: a SingleReadable selection failure is HostError and records no ScriptExecuted", "[l2.4][l2-c1]") {
    // The candidate is a lifecycle member (script granted) but cannot read it.
    Lab lab;
    auto isolated = std::make_shared<Isolated>();
    const EvaluationFixtureId fixture{"unreadable-script", 1};
    lab.register_suite(probe_suite(fixture,
        selection_factory(isolated, Selection::SingleReadable, ComponentAccessMode::Write)));
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource), fixture);
    require_stopped_with(record, "lifecycle script selection: no readable LuaBehaviorScript component");
    const EvaluationTrace trace = trace_of(lab.session, record.id);
    REQUIRE_FALSE(trace.entries.empty());
    CHECK_FALSE(any_script_executed(trace));
}

TEST_CASE("L2 c1: a SingleReadable candidate without a script grant records no ScriptExecuted", "[l2.4][l2-c1]") {
    Lab lab;
    auto isolated = std::make_shared<Isolated>();
    FactoryVariant noScript;
    noScript.omitScriptGrant = true;
    const EvaluationFixtureId fixture{"no-script-trace", 1};
    lab.register_suite(probe_suite(fixture, lighting_factory(isolated, &lab.host.world, noScript)));
    lab.register_lighting();
    const ProposalId proposal = lab.submit(ReferenceSource);

    const EvaluationRecord record = lab.evaluate(proposal, fixture);
    require_stopped_with(record, NoLifecycleResult);
    const EvaluationTrace trace = trace_of(lab.session, record.id);
    REQUIRE_FALSE(trace.entries.empty());
    CHECK_FALSE(any_script_executed(trace));

    // The same proposal on the passing fixture does record script execution.
    CHECK(any_script_executed(trace_of(lab.session, lab.evaluate(proposal).id)));
}

TEST_CASE("L2 c1: a repeated evaluation in one session keeps its session and proposal", "[l2.4][l2-c1]") {
    Lab lab;
    lab.register_lighting();
    const ProposalId proposal = lab.submit(ReferenceSource);
    const EvaluationRecord original = lab.evaluate(proposal);
    const EvaluationRecord repeated = lab.evaluate(proposal);

    REQUIRE(original.status == EvaluationStatus::Passed);
    CHECK(repeated.id != original.id);
    CHECK(original.session == lab.session.id());
    CHECK(original.proposal == proposal);
    CHECK(repeated.session == original.session);
    CHECK(repeated.proposal == original.proposal);
    require_same_evaluation(original, repeated);
}

// =====================================================================
// L2 b14 — scoped frame summaries and host-only whole-world totals
// =====================================================================

namespace {

// The b14 fixture: a host competitor commands the ungranted hallway through
// the case adapter while the candidate commands office.
EvaluationSuite competitor_suite(const EvaluationFixtureId& fixture, const std::shared_ptr<Isolated>& isolated, const World* liveWorld) {
    const EvaluationCaseFactory base = lighting_factory(isolated, liveWorld);
    const EvaluationCaseFactory factory = [isolated, base](const EvaluationPreparation& prep) {
        PreparedEvaluation prepared = base(prep);
        World& world = prepared.runtime->world();
        prepared.runtime->bind_effect_component(isolated->light, "hallway", EffectTarget{"hallway-device"});
        const BehaviorId competitor = world.create_behavior();
        world.grant_component_access(isolated->light, competitor, "hallway", ComponentAccessMode::ReadWrite);
        world.create_intent(competitor, isolated->light, world.get_components(isolated->light, competitor).at("hallway"),
            IntentLifetime::persistent(), Light{90}, IntentPriority::High, "hallway");
        return prepared;
    };
    EvaluationSuite suite = probe_suite(fixture, factory);
    for (EvaluationCase& evaluationCase : suite.cases)
        evaluationCase.adapters = {AdapterBehavior{}};
    return suite;
}

EvaluationWorldTotals world_totals_of(const AuthoringSession& session, EvaluationId evaluation) {
    const auto totals = session.world_totals(evaluation);
    INFO((totals.ok() ? std::string{} : totals.error().diagnostic));
    REQUIRE(totals.ok());
    return totals.value();
}

ExternalObservation light_observation(SessionId session, std::string target, std::int64_t level) {
    return ExternalObservation{
        session, AdapterRoute{std::string(LightRoute)}, EffectTarget{std::move(target)}, Value{level}, StateRevision{1}, 0};
}

}

TEST_CASE("L2 b14: world_totals counts the whole isolated world while the record counts the candidate's scope", "[l2-b14]") {
    Lab lab;
    const EvaluationFixtureId fixture{"b14-competitor", 1};
    lab.register_suite(competitor_suite(fixture, std::make_shared<Isolated>(), &lab.host.world));

    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource), fixture);
    REQUIRE(record.status == EvaluationStatus::Passed);
    REQUIRE(record.cases.size() == 2);
    const EvaluationWorldTotals totals = world_totals_of(lab.session, record.id);
    CHECK(totals.evaluation == record.id);
    CHECK(totals.complete);
    REQUIRE(totals.cases.size() == 2);
    for (std::size_t index = 0; index < 2; ++index) {
        INFO("case " << index);
        const EvaluationCaseResult& scoped = record.cases[index];
        REQUIRE(scoped.frames.size() == 2);
        // Candidate scope: the office command and its one report.
        CHECK(scoped.frames[0].commandsIssued == 1);
        CHECK(scoped.frames[0].reportStatuses.empty());
        CHECK(scoped.frames[1].commandsIssued == 0);
        CHECK(scoped.frames[1].reportStatuses == std::vector<CommandStatus>{CommandStatus::Applied});
        // Whole world: the competitor's hallway command and report too.
        REQUIRE(totals.cases[index].frames.size() == 2);
        CHECK(totals.cases[index].frames[0] == EvaluationWorldFrameTotals{0, 2, 0, 0});
        CHECK(totals.cases[index].frames[1] == EvaluationWorldFrameTotals{10, 0, 2, 0});
    }
}

TEST_CASE("L2 b14: observations on a Write-only granted target count only in world_totals", "[l2-b14]") {
    Lab lab;
    auto isolated = std::make_shared<Isolated>();
    auto session = std::make_shared<SessionId>();
    const EvaluationCaseFactory base = lighting_factory(isolated, &lab.host.world);
    const EvaluationCaseFactory factory = [isolated, session, base](const EvaluationPreparation& prep) {
        PreparedEvaluation prepared = base(prep);
        *session = prep.runtimeOptions.sessionId;
        prepared.runtime->bind_effect_component(isolated->light, "porch", EffectTarget{"porch-device"});
        prepared.grantedEffects.push_back(
            {"Light", "porch", AdapterRoute{std::string(LightRoute)}, EffectTarget{"porch-device"}});
        return prepared;
    };
    // Frame 0: one observation on the Write-only porch, one on ReadWrite office.
    const EvaluationInputAction observe = [session](PreparedEvaluation& prepared, const EvaluationFrameContext& context) {
        if (context.frameIndex != 0)
            return;
        const FeedbackSender sender = prepared.runtime->feedback_sender();
        REQUIRE(sender.try_send(light_observation(*session, "porch-device", 40)) == FeedbackSendResult::Sent);
        REQUIRE(sender.try_send(light_observation(*session, std::string(OfficeDevice), 70)) == FeedbackSendResult::Sent);
    };
    const EvaluationFixtureId fixture{"b14-porch-observation", 1};
    EvaluationSuite suite = probe_suite(fixture, factory, observe);
    for (EvaluationCase& evaluationCase : suite.cases)
        evaluationCase.adapters = {AdapterBehavior{}};
    lab.register_suite(std::move(suite));

    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource), fixture);
    INFO("case 0: " << (record.cases.empty() ? std::string{} : record.cases[0].diagnostic));
    REQUIRE(record.status == EvaluationStatus::Passed);
    const EvaluationWorldTotals totals = world_totals_of(lab.session, record.id);
    CHECK(totals.complete);
    REQUIRE(totals.cases.size() == 2);
    for (std::size_t index = 0; index < 2; ++index) {
        INFO("case " << index);
        REQUIRE(record.cases[index].frames.size() == 2);
        // Both observations were accepted by the isolated Runtime.
        REQUIRE(totals.cases[index].frames.size() == 2);
        CHECK(totals.cases[index].frames[0].observations == 2);
        // The summary counts office only; porch is never readable.
        CHECK(record.cases[index].frames[0].observations == 1);
        CHECK(record.cases[index].frames[1].observations == 0);
        CHECK(totals.cases[index].frames[1].observations == 0);
    }
    // The Write-only target still exposes no value.
    const auto porch = std::find_if(record.cases[0].finalState.begin(), record.cases[0].finalState.end(),
        [](const EvaluationTargetSummary& target) { return target.componentName == "porch"; });
    REQUIRE(porch != record.cases[0].finalState.end());
    CHECK_FALSE(porch->value.has_value());
}

TEST_CASE("L2 b14: world_totals returns NotFound for an unknown evaluation", "[l2-b14]") {
    Lab lab;
    const AuthoringError before = require_error(lab.session.world_totals(EvaluationId{1}), AuthoringErrorCode::NotFound);
    CHECK(before.fieldPath == std::optional<std::string>{"evaluation"});
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource));
    REQUIRE(lab.session.world_totals(record.id).ok());
    require_error(lab.session.world_totals(EvaluationId{99}), AuthoringErrorCode::NotFound);
    require_error(lab.session.world_totals(EvaluationId{}), AuthoringErrorCode::NotFound);
}

TEST_CASE("L2 b14: on lighting-v1 every effect is the candidate's, so world_totals equals the summaries", "[l2-b14]") {
    Lab lab;
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource));
    REQUIRE(record.status == EvaluationStatus::Passed);
    const EvaluationWorldTotals totals = world_totals_of(lab.session, record.id);
    CHECK(totals.complete);
    REQUIRE(totals.cases.size() == record.cases.size());
    std::size_t commands = 0;
    for (std::size_t index = 0; index < record.cases.size(); ++index) {
        INFO("case " << record.cases[index].name);
        const std::vector<EvaluationFrameSummary>& scoped = record.cases[index].frames;
        REQUIRE(totals.cases[index].frames.size() == scoped.size());
        for (std::size_t frame = 0; frame < scoped.size(); ++frame) {
            INFO("frame " << frame);
            CHECK(totals.cases[index].frames[frame] == EvaluationWorldFrameTotals{
                scoped[frame].now, scoped[frame].commandsIssued, scoped[frame].reportStatuses.size(), scoped[frame].observations});
            commands += scoped[frame].commandsIssued;
        }
    }
    // The suite does issue commands, so the equality is not vacuous.
    CHECK(commands > 0);
}

TEST_CASE("L2 b14: world_totals is incomplete when the run budget or response bound truncated the evidence", "[l2-b14]") {
    // Case 0 of an unbounded run: records before its first frame, and all.
    std::size_t setupRecords = 0;
    std::size_t firstCaseRecords = 0;
    {
        Lab lab;
        lab.register_lighting();
        const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource));
        REQUIRE(record.status == EvaluationStatus::Passed);
        REQUIRE(record.cases.size() > 1);
        const EvaluationTrace trace = trace_of(lab.session, record.id);
        const auto started = std::find_if(trace.entries.begin(), trace.entries.end(),
            [](const EvaluationTraceEntry& entry) { return entry.type == EventType::FrameStarted; });
        REQUIRE(started != trace.entries.end());
        setupRecords = static_cast<std::size_t>(started - trace.entries.begin());
        firstCaseRecords = static_cast<std::size_t>(std::count_if(trace.entries.begin(), trace.entries.end(),
            [](const EvaluationTraceEntry& entry) { return entry.caseIndex == 0; }));
        REQUIRE(setupRecords > 0);
        REQUIRE(firstCaseRecords > setupRecords + 2);
    }
    // Refused at case 0's FrameStarted, inside its first frame, and in case
    // 1's setup after case 0 passed within the budget.
    for (const std::size_t maxRecords : {setupRecords, setupRecords + 2, firstCaseRecords}) {
        INFO("maxEvaluationRecords " << maxRecords);
        AuthoringLimits limits;
        limits.maxEvaluationRecords = maxRecords;
        Lab lab(limits);
        lab.register_lighting();
        const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource));
        REQUIRE(record.status == EvaluationStatus::LimitExceeded);
        const std::size_t refused = maxRecords == firstCaseRecords ? 1 : 0;
        REQUIRE(record.cases.size() > refused);
        if (refused == 1)
            CHECK(record.cases[0].status == EvaluationCaseStatus::Passed);
        CHECK(record.cases[refused].status == EvaluationCaseStatus::LimitExceeded);
        CHECK(record.cases[refused].framesRun == (refused == 1 ? 0U : 1U));
        CHECK_FALSE(world_totals_of(lab.session, record.id).complete);
    }
    AuthoringLimits limits;
    limits.maxEvaluationResponseBytes = 64;
    Lab lab(limits);
    lab.register_lighting();
    const EvaluationRecord record = lab.evaluate(lab.submit(ReferenceSource));
    REQUIRE_FALSE(record.complete);
    const EvaluationWorldTotals totals = world_totals_of(lab.session, record.id);
    CHECK_FALSE(totals.complete);
    // The kept trace still yields every case's frames.
    CHECK_FALSE(totals.cases.empty());
}

TEST_CASE("L2 b14: a granted-effect address must name a captured grant and its own target", "[l2-b14]") {
    Lab lab;
    auto isolated = std::make_shared<Isolated>();
    const EvaluationCaseFactory base = lighting_factory(isolated, &lab.host.world);
    const auto declaring = [isolated, base](EvaluationEffectAddress address, bool bindHallway) {
        return [isolated, base, address, bindHallway](const EvaluationPreparation& prep) {
            PreparedEvaluation prepared = base(prep);
            if (bindHallway)
                prepared.runtime->bind_effect_component(isolated->light, "hallway", EffectTarget{"hallway-device"});
            prepared.grantedEffects.push_back(address);
            return prepared;
        };
    };
    const AdapterRoute route{std::string(LightRoute)};
    const EvaluationFixtureId ungranted{"b14-ungranted-address", 1};
    const EvaluationFixtureId repeated{"b14-repeated-address", 1};
    for (const auto& [fixture, factory] : {
             std::pair{ungranted, EvaluationCaseFactory(declaring({"Light", "hallway", route, EffectTarget{"hallway-device"}}, true))},
             std::pair{repeated, EvaluationCaseFactory(declaring({"Light", "porch", route, EffectTarget{std::string(OfficeDevice)}}, false))}}) {
        EvaluationSuite suite = probe_suite(fixture, factory);
        for (EvaluationCase& evaluationCase : suite.cases)
            evaluationCase.adapters = {AdapterBehavior{}};
        lab.register_suite(std::move(suite));
    }
    const ProposalId proposal = lab.submit(ReferenceSource);

    const EvaluationRecord first = lab.evaluate(proposal, ungranted);
    CHECK(first.status == EvaluationStatus::HostError);
    REQUIRE(first.cases.size() == 2);
    CHECK(first.cases[0].status == EvaluationCaseStatus::HostError);
    CHECK(first.cases[0].framesRun == 0);
    CHECK(first.cases[0].diagnostic == "fixture mismatch: effect address for an ungranted component");
    CHECK(first.cases[1].status == EvaluationCaseStatus::NotRun);

    const EvaluationRecord second = lab.evaluate(proposal, repeated);
    CHECK(second.status == EvaluationStatus::HostError);
    REQUIRE_FALSE(second.cases.empty());
    CHECK(second.cases[0].framesRun == 0);
    // Never names the adapter route or target.
    CHECK(second.cases[0].diagnostic == "fixture mismatch: effect address for Light.porch repeats another target");
}

// L2 r9 (review c3): an address diagnostic never names a component outside
// the candidate's scope, and a granted component claims at most one address.
TEST_CASE("L2 b14: granted-effect diagnostics hide ungranted names and allow one address per component", "[l2-b14]") {
    Lab lab;
    auto isolated = std::make_shared<Isolated>();
    const EvaluationCaseFactory base = lighting_factory(isolated, &lab.host.world);
    const AdapterRoute route{std::string(LightRoute)};
    const EvaluationFixtureId hidden{"b14-hidden-ungranted-name", 1};
    const EvaluationFixtureId twice{"b14-two-addresses-one-component", 1};
    const EvaluationCaseFactory hiddenFactory = [isolated, base, route](const EvaluationPreparation& prep) {
        PreparedEvaluation prepared = base(prep);
        prepared.runtime->bind_effect_component(isolated->light, "hallway", EffectTarget{"hallway-device"});
        prepared.grantedEffects.push_back({"Light", "hallway", route, EffectTarget{"hallway-device"}});
        return prepared;
    };
    // The office grant is already declared on the office device; a second
    // address on a distinct target would let it count that target too.
    const EvaluationCaseFactory twiceFactory = [base, route](const EvaluationPreparation& prep) {
        PreparedEvaluation prepared = base(prep);
        prepared.grantedEffects.push_back({"Light", "office", route, EffectTarget{"porch-device"}});
        return prepared;
    };
    for (const auto& [fixture, factory] : {std::pair{hidden, hiddenFactory}, std::pair{twice, twiceFactory}}) {
        EvaluationSuite suite = probe_suite(fixture, factory);
        for (EvaluationCase& evaluationCase : suite.cases)
            evaluationCase.adapters = {AdapterBehavior{}};
        lab.register_suite(std::move(suite));
    }
    const ProposalId proposal = lab.submit(ReferenceSource);

    const EvaluationRecord first = lab.evaluate(proposal, hidden);
    CHECK(first.status == EvaluationStatus::HostError);
    REQUIRE_FALSE(first.cases.empty());
    CHECK(first.cases[0].status == EvaluationCaseStatus::HostError);
    {
        INFO("diagnostic " << first.cases[0].diagnostic);
        CHECK(first.cases[0].diagnostic.find("hallway") == std::string::npos);
    }

    const EvaluationRecord second = lab.evaluate(proposal, twice);
    CHECK(second.status == EvaluationStatus::HostError);
    REQUIRE(second.cases.size() == 2);
    CHECK(second.cases[0].status == EvaluationCaseStatus::HostError);
    CHECK(second.cases[0].framesRun == 0);
    CHECK(second.cases[0].diagnostic == "fixture mismatch: effect address for Light.office repeats its component");
    CHECK(second.cases[1].status == EvaluationCaseStatus::NotRun);
}
