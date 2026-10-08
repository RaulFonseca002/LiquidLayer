#include <liquid/Runtime.hpp>
#include <liquid/authoring/AuthoringSession.hpp>
#include <liquid/authoring/Evaluation.hpp>
#include <liquid/authoring/Types.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace {

namespace authoring = liquid::authoring;
namespace scripting = liquid::scripting;

struct Light {
    std::int64_t level = 0;
};

liquid::ComponentType<Light> register_light(liquid::World& world) {
    return world.register_component<Light>(
        "consumer.Light",
        1,
        liquid::ComponentCodec<Light>{
            [](const Light& light) { return liquid::Value{light.level}; },
            [](const liquid::Value& value) { return Light{value.as_signed_integer()}; }
        });
}

liquid::ComponentType<scripting::LuaBehaviorScript> register_script(liquid::World& world) {
    return world.register_component<scripting::LuaBehaviorScript>(
        "liquid.LuaBehaviorScript", 1, scripting::lua_behavior_script_codec());
}

// The live session and every isolated case expose Light identically.
void expose_light(scripting::LuaBehaviorRunner& runner, liquid::ComponentType<Light> lightType) {
    using Schema = scripting::LuaValueSchema;
    runner.expose_component(
        lightType,
        "Light",
        scripting::LuaComponentCodec<Light>{
            [](const Light& light) {
                return scripting::LuaValue{scripting::LuaValue::Table{{"level", scripting::LuaValue{light.level}}}};
            },
            [](const scripting::LuaValue& value) {
                return Light{value.as_table().at("level").as_integer()};
            }
        },
        scripting::symmetric_metadata(
            Schema::object({{"level", Schema::integer(0, 100), true}}),
            "Office brightness"));
}

// One fresh isolated world per case: the office, the proposal's script and a
// SingleReadable lifecycle system.
authoring::PreparedEvaluation prepare_office(const authoring::EvaluationPreparation& prep) {
    authoring::PreparedEvaluation prepared;
    prepared.runtime = std::make_unique<liquid::Runtime>(prep.runtimeOptions);
    liquid::World& world = prepared.runtime->world();
    const auto lightType = register_light(world);
    prepared.scriptType = register_script(world);
    world.add_component(lightType, "office", Light{40});
    world.add_component(prepared.scriptType, "lifecycle", scripting::LuaBehaviorScript{prep.source, 1});

    prepared.candidate = world.create_behavior();
    world.grant_component_access(lightType, prepared.candidate, "office", liquid::ComponentAccessMode::ReadWrite);
    world.grant_component_access(
        prepared.scriptType, prepared.candidate, "lifecycle", liquid::ComponentAccessMode::Read);

    prepared.runner = std::make_shared<scripting::LuaBehaviorRunner>();
    expose_light(*prepared.runner, lightType);
    liquid::Signature signature;
    signature.set(prepared.scriptType.id);
    world.register_system<scripting::LuaLifecycleSystem>(
        signature,
        liquid::SystemPhase::Behavior,
        prepared.scriptType,
        prepared.runner,
        scripting::LuaScriptSelection::SingleReadable);
    prepared.runtime->configure_component(lightType, "office", liquid::ComponentControl::InternalState);
    return prepared;
}

authoring::EvaluationSuite office_suite() {
    authoring::EvaluationCase officeCase;
    officeCase.name = "office reaches 70";
    officeCase.frameTimes = {100, 110};
    officeCase.prepare = prepare_office;
    officeCase.expect = [](const authoring::PreparedEvaluation&, const authoring::EvaluationFrameContext& context) {
        const bool ran = context.lifecycle != nullptr && context.lifecycle->succeeded();
        return authoring::EvaluationAssertion{ran, ran ? std::string{} : "lifecycle failed"};
    };

    authoring::EvaluationSuite suite;
    suite.id = authoring::EvaluationFixtureId{"consumer-office", 1};
    suite.cases.push_back(std::move(officeCase));
    return suite;
}

}

int main() {
    liquid::Runtime runtime;
    liquid::World& world = runtime.world();

    const auto lightType = register_light(world);
    const auto scriptType = register_script(world);
    world.add_component(lightType, "office", Light{40});

    scripting::LuaBehaviorRunner runner;
    expose_light(runner, lightType);

    const auto sessionId = authoring::AuthoringSessionId::from_hex("0123456789abcdef0123456789abcdef");
    if (!sessionId.ok())
        return 1;

    authoring::AuthoringSession session(sessionId.value(), runtime, runner, scriptType);
    // Suites register before the first accepted proposal.
    if (!session.register_evaluation_suite(office_suite()).ok())
        return 6;
    const auto scope = session.create_scope(
        "consumer",
        {authoring::ScopeGrant{"Light", "office", liquid::ComponentAccessMode::ReadWrite}},
        100);
    if (!scope.ok() || scope.value().manifest.capabilities().size() != 1)
        return 2;

    const authoring::CallerContext caller{sessionId.value(), "consumer"};
    const std::string source =
        "function on_start(frame)\n"
        "    access.Light.office.propose{name = \"dim\", value = {level = 70}, priority = \"low\", lifetime = \"persistent\"}\n"
        "end\n";
    authoring::ProposalSubmission submission;
    submission.scope = scope.value().scope;
    submission.expectedScopeRevision = scope.value().revision;
    submission.source = source;
    const auto proposal = session.submit(caller, std::move(submission));
    if (!proposal.ok())
        return 3;

    const auto record = session.proposal(caller, proposal.value());
    if (!record.ok() || record.value().source != source)
        return 4;

    const auto evaluation = session.evaluate(caller, proposal.value(), {"consumer-office", 1});
    if (!evaluation.ok() || evaluation.value().status != authoring::EvaluationStatus::Passed)
        return 7;
    const auto& finalState = evaluation.value().cases.front().finalState;
    const liquid::Value dimmed{liquid::Value::Object{{"level", liquid::Value{std::int64_t{70}}}}};
    if (finalState.size() != 1 || finalState.front().value != dimmed)
        return 8;

    // Admission and evaluation never run Lua in, or change, the live World.
    return world.behavior_count() == 0 && world.live_intent_ids().empty()
        && world.get_component_named(lightType, "office")->level == 40 ? 0 : 5;
}
