#include <liquid/Runtime.hpp>
#include <liquid/authoring/AuthoringSession.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace {

struct Light {
    std::int64_t level = 0;
};

}

int main() {
    namespace authoring = liquid::authoring;
    namespace scripting = liquid::scripting;
    using Schema = scripting::LuaValueSchema;

    liquid::Runtime runtime;
    liquid::World& world = runtime.world();

    const auto lightType = world.register_component<Light>(
        "consumer.Light",
        1,
        liquid::ComponentCodec<Light>{
            [](const Light& light) { return liquid::Value{light.level}; },
            [](const liquid::Value& value) { return Light{value.as_signed_integer()}; }
        });
    const auto scriptType = world.register_component<scripting::LuaBehaviorScript>(
        "liquid.LuaBehaviorScript", 1, scripting::lua_behavior_script_codec());
    world.add_component(lightType, "office", Light{40});

    scripting::LuaBehaviorRunner runner;
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

    const auto sessionId = authoring::AuthoringSessionId::from_hex("0123456789abcdef0123456789abcdef");
    if (!sessionId.ok())
        return 1;

    authoring::AuthoringSession session(sessionId.value(), runtime, runner, scriptType);
    const auto scope = session.create_scope(
        "consumer",
        {authoring::ScopeGrant{"Light", "office", liquid::ComponentAccessMode::ReadWrite}},
        100);
    if (!scope.ok() || scope.value().manifest.capabilities().size() != 1)
        return 2;

    const authoring::CallerContext caller{sessionId.value(), "consumer"};
    const std::string source = "access.Light.office.propose({ value = { level = 70 } })\n";
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

    // Admission never runs Lua or changes World.
    return world.behavior_count() == 0 && world.live_intent_ids().empty() ? 0 : 5;
}
