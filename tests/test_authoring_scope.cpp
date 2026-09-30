#include "liquid/Runtime.hpp"
#include "liquid/authoring/AuthoringSession.hpp"
#include "liquid/authoring/Types.hpp"
#include "liquid/scripting/LuaCapabilityManifest.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace liquid;
using namespace liquid::authoring;
using namespace liquid::scripting;

namespace {

using Schema = LuaValueSchema;

template <int Tag>
struct Tagged {
    std::int64_t level = 0;
};

using Light = Tagged<0>;
using Fan = Tagged<1>;
using Lamp = Tagged<2>;
using Late = Tagged<3>;
using Raw = Tagged<4>;

template <int Tag>
ComponentCodec<Tagged<Tag>> tagged_codec() {
    return {
        [](const Tagged<Tag>& value) { return Value{value.level}; },
        [](const Value& value) { return Tagged<Tag>{value.as_signed_integer()}; }
    };
}

template <int Tag>
LuaComponentCodec<Tagged<Tag>> lua_tagged_codec() {
    return {
        [](const Tagged<Tag>& value) {
            return LuaValue{LuaValue::Table{{"level", LuaValue{value.level}}}};
        },
        [](const LuaValue& value) {
            return Tagged<Tag>{value.as_table().at("level").as_integer()};
        }
    };
}

template <int Tag>
LuaComponentCodec<Tagged<Tag>> counting_codec(std::size_t& encodes, std::size_t& decodes) {
    return {
        [&encodes](const Tagged<Tag>& value) {
            ++encodes;
            return LuaValue{LuaValue::Table{{"level", LuaValue{value.level}}}};
        },
        [&decodes](const LuaValue& value) {
            ++decodes;
            return Tagged<Tag>{value.as_table().at("level").as_integer()};
        }
    };
}

// 0 encodes normally; any other mode throws.
template <int Tag>
LuaComponentCodec<Tagged<Tag>> throwing_codec(const int& mode) {
    return {
        [&mode](const Tagged<Tag>& value) {
            if (mode != 0)
                throw std::runtime_error("snapshot failed");
            return LuaValue{LuaValue::Table{{"level", LuaValue{value.level}}}};
        },
        [](const LuaValue& value) {
            return Tagged<Tag>{value.as_table().at("level").as_integer()};
        }
    };
}

Schema level_schema() {
    return Schema::object({{"level", Schema::integer(0, 100), true}});
}

template <int Tag>
struct Note {
    std::string text;
};

template <int Tag>
ComponentCodec<Note<Tag>> note_codec() {
    return {
        [](const Note<Tag>& note) { return Value{note.text}; },
        [](const Value& value) { return Note<Tag>{value.as_string()}; }
    };
}

template <int Tag>
LuaComponentCodec<Note<Tag>> lua_note_codec() {
    return {
        [](const Note<Tag>& note) { return LuaValue{LuaValue::Table{{"text", LuaValue{note.text}}}}; },
        [](const LuaValue& value) { return Note<Tag>{value.as_table().at("text").as_string()}; }
    };
}

Schema note_schema() {
    return Schema::object({{"text", Schema::string(0, 1000), true}});
}

LuaComponentCodec<LuaBehaviorScript> lua_script_codec() {
    return {
        [](const LuaBehaviorScript& script) {
            return LuaValue{LuaValue::Table{{"source", LuaValue{script.source}}}};
        },
        [](const LuaValue& value) {
            return LuaBehaviorScript{value.as_table().at("source").as_string(), 1};
        }
    };
}

Schema script_schema() {
    return Schema::object({{"source", Schema::string(0, 1000), true}});
}

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

LuaManifestError require_manifest_error(const LuaManifestResult& result, LuaManifestErrorCode code) {
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == code);
    return result.error();
}

const LuaManifestCapability* find_capability(const LuaCapabilityManifest& manifest, const std::string& type) {
    for (const LuaManifestCapability& capability : manifest.capabilities()) {
        if (capability.scriptTypeName == type)
            return &capability;
    }
    return nullptr;
}

std::int64_t read_level(const LuaManifestCapability& capability) {
    REQUIRE(capability.readValue.has_value());
    return capability.readValue->as_table().at("level").as_integer();
}

// World with described Light/Fan/Lamp bindings, a schema-less Raw binding,
// the script-control type (registered and exposed), and a Late type left
// unexposed so tests can probe the registration freeze.
struct Fixture {
    Runtime runtime;
    World& world;
    ComponentType<Light> lightType;
    ComponentType<Fan> fanType;
    ComponentType<Lamp> lampType;
    ComponentType<Late> lateType;
    ComponentType<Raw> rawType;
    ComponentType<LuaBehaviorScript> scriptType;
    std::size_t fanEncodes = 0;
    std::size_t fanDecodes = 0;
    LuaBehaviorRunner runner;

    explicit Fixture(bool exposeScript = false)
        : world(runtime.world()),
          lightType(world.register_component<Light>("test.Light", 1, tagged_codec<0>())),
          fanType(world.register_component<Fan>("test.Fan", 1, tagged_codec<1>())),
          lampType(world.register_component<Lamp>("test.Lamp", 1, tagged_codec<2>())),
          lateType(world.register_component<Late>("test.Late", 1, tagged_codec<3>())),
          rawType(world.register_component<Raw>("test.Raw", 1, tagged_codec<4>())),
          scriptType(world.register_component<LuaBehaviorScript>(
              "liquid.LuaBehaviorScript", 1, lua_behavior_script_codec())) {
        world.add_component(lightType, "office", Light{40});
        world.add_component(fanType, "office", Fan{10});
        world.add_component(lampType, "desk", Lamp{5});
        world.add_component(lateType, "office", Late{1});
        world.add_component(rawType, "office", Raw{1});
        world.add_component(scriptType, "main", LuaBehaviorScript{"return 1", 1});
        runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema(), "Brightness"));
        runner.expose_component(fanType, "Fan", counting_codec<1>(fanEncodes, fanDecodes), symmetric_metadata(level_schema(), "Fan speed"));
        runner.expose_component(lampType, "Lamp", lua_tagged_codec<2>(), symmetric_metadata(level_schema(), "Desk lamp"));
        runner.expose_component(rawType, "Raw", lua_tagged_codec<4>());
        if (exposeScript)
            runner.expose_component(scriptType, "Script", lua_script_codec(), symmetric_metadata(script_schema(), "Script"));
        world.clear_topology_mutations();
    }
};

std::vector<ScopeGrant> light_grant(ComponentAccessMode mode = ComponentAccessMode::ReadWrite) {
    return {ScopeGrant{"Light", "office", mode}};
}

struct LiveCounts {
    std::size_t behaviors = 0;
    std::size_t systems = 0;
    std::size_t intents = 0;
    std::size_t topology = 0;

    bool operator==(const LiveCounts&) const = default;
};

LiveCounts live_counts(World& world) {
    return {world.behavior_count(), world.system_count(), world.live_intent_ids().size(), world.topology_mutations().size()};
}

void require_no_scope(AuthoringSession& session, const CallerContext& caller, std::uint64_t upTo, IntentTime now) {
    for (std::uint64_t raw = 1; raw <= upTo; ++raw)
        require_error(session.discover(caller, ScopeId{raw}, now), AuthoringErrorCode::NotFound);
}

}

// L1.1 Common IDs, results, limits and session construction

TEST_CASE("L1.1 session id accepts exactly 32 lowercase hexadecimal digits") {
    const auto valid = AuthoringSessionId::from_hex("0123456789abcdef0123456789abcdef");
    REQUIRE(valid.ok());
    CHECK(valid.value().hex() == "0123456789abcdef0123456789abcdef");
    CHECK_FALSE(valid.value() == session_id('0'));
    CHECK(session_id('a') == session_id('a'));

    for (const std::string& bad : {
             std::string("0123456789ABCDEF0123456789ABCDEF"),
             std::string("0123456789abcdef0123456789abcde"),
             std::string("0123456789abcdef0123456789abcdef0"),
             std::string("0123456789abcdeg0123456789abcdef"),
             std::string(),
             std::string(32, ' '),
             std::string("0123456789abcdef\0" "123456789abcdef", 32)}) {
        require_error(AuthoringSessionId::from_hex(bad), AuthoringErrorCode::InvalidInput);
    }
}

TEST_CASE("L1.1 strong ids are zero-invalid, typed and ordered") {
    static_assert(!std::is_convertible_v<ScopeId, ProposalId>);
    static_assert(!std::is_convertible_v<std::uint64_t, ScopeId>);
    CHECK_FALSE(ScopeId{}.valid());
    CHECK(ScopeId{}.value() == 0);
    CHECK(ScopeId{7}.valid());
    CHECK(ScopeId{7}.value() == 7);
    CHECK(ScopeId{3} < ScopeId{4});
    CHECK(ProposalId{5} == ProposalId{5});
    CHECK_FALSE(ManagedBehaviorId{}.valid());
}

TEST_CASE("L1.1 authoring result holds exactly one value or error and empty success is distinct") {
    const AuthoringResult<int> value(5);
    REQUIRE(value.ok());
    CHECK(value.value() == 5);
    CHECK_THROWS_AS(value.error(), std::logic_error);

    const AuthoringResult<int> failure(AuthoringError{AuthoringErrorCode::NotFound, "missing", std::string("scope")});
    REQUIRE_FALSE(failure.ok());
    CHECK(failure.error().code == AuthoringErrorCode::NotFound);
    CHECK(failure.error().fieldPath == std::optional<std::string>("scope"));
    CHECK_THROWS_AS(failure.value(), std::logic_error);

    const AuthoringResult<void> empty;
    CHECK(empty.ok());
    CHECK_THROWS_AS(empty.error(), std::logic_error);
    const AuthoringResult<void> emptyFailure(AuthoringError{AuthoringErrorCode::StaleScope, "stale", std::nullopt});
    REQUIRE_FALSE(emptyFailure.ok());
    CHECK(emptyFailure.error().code == AuthoringErrorCode::StaleScope);
}

TEST_CASE("L1.1 authoring limits default to the common contract") {
    const AuthoringLimits limits;
    CHECK(limits.maxActiveScopes == 64);
    CHECK(limits.maxProposals == 128);
    CHECK(limits.maxEvaluations == 256);
    CHECK(limits.maxManagedBehaviors == 64);
    CHECK(limits.maxSourceBytes == 65'536);
    CHECK(limits.maxRationaleBytes == 2'048);
    CHECK(limits.maxScopeCapabilities == 128);
    CHECK(limits.maxLabelBytes == 256);
    CHECK(limits.maxManifestBytes == 1024 * 1024);
    CHECK(AuthoringMaxDiagnosticBytes == 4'096);
}

TEST_CASE("L1.1 session constructor rejects a foreign or invalid script type and invalid limits") {
    Fixture fx;
    const AuthoringSessionId id = session_id();

    Runtime other;
    const auto foreignScript = other.world().register_component<LuaBehaviorScript>(
        "liquid.LuaBehaviorScript", 1, lua_behavior_script_codec());
    CHECK_THROWS_AS(AuthoringSession(id, fx.runtime, fx.runner, foreignScript), std::invalid_argument);
    CHECK_THROWS_AS(AuthoringSession(id, fx.runtime, fx.runner, ComponentType<LuaBehaviorScript>{}), std::invalid_argument);

    AuthoringLimits zeroScopes;
    zeroScopes.maxActiveScopes = 0;
    CHECK_THROWS_AS(AuthoringSession(id, fx.runtime, fx.runner, fx.scriptType, zeroScopes), std::invalid_argument);
    AuthoringLimits zeroProposals;
    zeroProposals.maxProposals = 0;
    CHECK_THROWS_AS(AuthoringSession(id, fx.runtime, fx.runner, fx.scriptType, zeroProposals), std::invalid_argument);
    AuthoringLimits raisedSource;
    raisedSource.maxSourceBytes = AuthoringLimits{}.maxSourceBytes + 1;
    CHECK_THROWS_AS(AuthoringSession(id, fx.runtime, fx.runner, fx.scriptType, raisedSource), std::invalid_argument);
    AuthoringLimits raisedRationale;
    raisedRationale.maxRationaleBytes = AuthoringLimits{}.maxRationaleBytes + 1;
    CHECK_THROWS_AS(AuthoringSession(id, fx.runtime, fx.runner, fx.scriptType, raisedRationale), std::invalid_argument);
    AuthoringLimits zeroIds;
    zeroIds.maxIdValue = 0;
    CHECK_THROWS_AS(AuthoringSession(id, fx.runtime, fx.runner, fx.scriptType, zeroIds), std::invalid_argument);

    AuthoringLimits lowered;
    lowered.maxActiveScopes = 1;
    lowered.maxSourceBytes = 16;
    const AuthoringSession session(id, fx.runtime, fx.runner, fx.scriptType, lowered);
    CHECK(session.id() == id);
    CHECK(session.limits().maxActiveScopes == 1);
    CHECK(session.limits().maxSourceBytes == 16);
}

// L1.2 Runner scope capture

TEST_CASE("L1.2 runner scope manifest projects Read, Write and ReadWrite without a behavior") {
    Fixture fx;
    const LiveCounts before = live_counts(fx.world);
    const std::vector<LuaScopeGrant> grants{
        {"Light", "office", ComponentAccessMode::ReadWrite},
        {"Fan", "office", ComponentAccessMode::Write},
        {"Lamp", "desk", ComponentAccessMode::Read}};

    const LuaManifestResult result = fx.runner.scope_manifest(fx.world, grants, 25);
    REQUIRE(result.ok());
    const LuaCapabilityManifest& manifest = result.manifest();
    CHECK(manifest.authoring_contract() == LuaAuthoringContract);
    CHECK(manifest.now_ms() == 25);
    REQUIRE(manifest.capabilities().size() == 3);

    const LuaManifestCapability* light = find_capability(manifest, "Light");
    REQUIRE(light != nullptr);
    CHECK(light->mode == ComponentAccessMode::ReadWrite);
    CHECK(read_level(*light) == 40);
    CHECK(light->readSchema.has_value());
    CHECK(light->writeSchema.has_value());
    CHECK_FALSE(light->accessExpression.empty());

    const LuaManifestCapability* fan = find_capability(manifest, "Fan");
    REQUIRE(fan != nullptr);
    CHECK(fan->mode == ComponentAccessMode::Write);
    CHECK_FALSE(fan->readValue.has_value());
    CHECK_FALSE(fan->readSchema.has_value());
    CHECK(fan->writeSchema.has_value());
    CHECK(fx.fanEncodes == 0);
    CHECK(fx.fanDecodes == 0);

    const LuaManifestCapability* lamp = find_capability(manifest, "Lamp");
    REQUIRE(lamp != nullptr);
    CHECK(lamp->mode == ComponentAccessMode::Read);
    CHECK(read_level(*lamp) == 5);
    CHECK_FALSE(lamp->writeSchema.has_value());

    const LuaManifestCapture& capture = manifest.capture();
    CHECK(capture.kind() == LuaManifestCaptureKind::ProspectiveScope);
    CHECK(capture.behavior() == BehaviorId{});
    CHECK(capture.world_instance() == fx.world.instance_id());
    CHECK(capture.entries().size() == 3);
    CHECK(live_counts(fx.world) == before);
}

TEST_CASE("L1.2 runner scope and behavior manifests share the binding expressions and schemas") {
    Fixture fx;
    const BehaviorId behavior = fx.world.create_behavior();
    fx.world.grant_component_access(fx.lightType, behavior, "office", ComponentAccessMode::ReadWrite);

    const LuaManifestResult scope = fx.runner.scope_manifest(fx.world, std::vector<LuaScopeGrant>(light_grant()), 3);
    const LuaManifestResult live = fx.runner.capability_manifest(fx.world, behavior, 3);
    REQUIRE(scope.ok());
    REQUIRE(live.ok());
    REQUIRE(scope.manifest().capabilities().size() == 1);
    REQUIRE(live.manifest().capabilities().size() == 1);
    const LuaManifestCapability& a = scope.manifest().capabilities().front();
    const LuaManifestCapability& b = live.manifest().capabilities().front();
    CHECK(a.accessExpression == b.accessExpression);
    CHECK(a.description == b.description);
    CHECK(a.readValue == b.readValue);
    CHECK(scope.manifest().capture().runner_id() == live.manifest().capture().runner_id());
    CHECK(live.manifest().capture().kind() == LuaManifestCaptureKind::Behavior);
}

TEST_CASE("L1.2 runner scope manifest rejects empty, duplicate, unnamed, unknown, schema-less and missing grants") {
    Fixture fx;
    const auto invalid = [&](std::vector<LuaScopeGrant> grants) {
        require_manifest_error(fx.runner.scope_manifest(fx.world, grants, 1), LuaManifestErrorCode::InvalidGrant);
    };
    invalid({});
    invalid({{"Light", "office", ComponentAccessMode::Read}, {"Light", "office", ComponentAccessMode::Write}});
    invalid({{"Light", "", ComponentAccessMode::Read}});
    invalid({{"", "office", ComponentAccessMode::Read}});
    invalid({{"Nope", "office", ComponentAccessMode::Read}});
    invalid({{"Raw", "office", ComponentAccessMode::Read}});
    invalid({{"Light", "attic", ComponentAccessMode::Read}});
    invalid({{"Light", "office", ComponentAccessMode::Read}, {"Nope", "office", ComponentAccessMode::Read}});

    // No failed capture froze registration (L0 rule).
    REQUIRE_NOTHROW(fx.runner.expose_component(fx.lateType, "Late", lua_tagged_codec<3>(), symmetric_metadata(level_schema())));
    REQUIRE(fx.runner.scope_manifest(fx.world, std::vector<LuaScopeGrant>{{"Late", "office", ComponentAccessMode::Read}}, 1).ok());
    CHECK_THROWS_AS(fx.runner.expose_component(fx.scriptType, "Script", lua_script_codec(), symmetric_metadata(script_schema())), std::logic_error);
}

TEST_CASE("L1.2 runner scope manifest rejects host time outside the Lua integer range") {
    Fixture fx;
    const IntentTime tooLate = static_cast<IntentTime>(std::numeric_limits<std::int64_t>::max()) + 1;
    require_manifest_error(fx.runner.scope_manifest(fx.world, std::vector<LuaScopeGrant>(light_grant()), tooLate), LuaManifestErrorCode::HostError);
}

// L1.2 Session scopes

TEST_CASE("L1.2 create, replace, discover and revoke change no live behavior, system, intent or topology") {
    Fixture fx;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType);
    const CallerContext alice{session.id(), "alice"};
    const LiveCounts before = live_counts(fx.world);

    const auto created = session.create_scope("alice", light_grant(), 10);
    REQUIRE(created.ok());
    CHECK(created.value().scope.valid());
    CHECK(created.value().revision >= 1);
    REQUIRE(created.value().manifest.capabilities().size() == 1);
    CHECK(created.value().manifest.capture().kind() == LuaManifestCaptureKind::ProspectiveScope);
    CHECK(live_counts(fx.world) == before);

    REQUIRE(session.discover(alice, created.value().scope, 11).ok());
    const auto replaced = session.replace_scope(created.value().scope, created.value().revision, light_grant(ComponentAccessMode::Read), 12);
    REQUIRE(replaced.ok());
    REQUIRE(session.revoke_scope(created.value().scope, replaced.value().revision).ok());
    CHECK(live_counts(fx.world) == before);
}

TEST_CASE("L1.2 session scope projects Write-only grants without reading them") {
    Fixture fx;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType);
    const CallerContext alice{session.id(), "alice"};
    const auto created = session.create_scope(
        "alice",
        {ScopeGrant{"Fan", "office", ComponentAccessMode::Write}, ScopeGrant{"Lamp", "desk", ComponentAccessMode::Read}},
        1);
    REQUIRE(created.ok());
    const LuaManifestCapability* fan = find_capability(created.value().manifest, "Fan");
    REQUIRE(fan != nullptr);
    CHECK_FALSE(fan->readValue.has_value());
    const LuaManifestCapability* lamp = find_capability(created.value().manifest, "Lamp");
    REQUIRE(lamp != nullptr);
    CHECK(read_level(*lamp) == 5);

    REQUIRE(session.discover(alice, created.value().scope, 2).ok());
    CHECK(fx.fanEncodes == 0);
    CHECK(fx.fanDecodes == 0);
}

TEST_CASE("L1.2 two callers cannot see each other's scopes") {
    Fixture fx;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType);
    const CallerContext alice{session.id(), "alice"};
    const CallerContext bob{session.id(), "bob"};
    const auto aliceScope = session.create_scope("alice", light_grant(), 1);
    const auto bobScope = session.create_scope("bob", {ScopeGrant{"Lamp", "desk", ComponentAccessMode::Read}}, 1);
    REQUIRE(aliceScope.ok());
    REQUIRE(bobScope.ok());
    CHECK(aliceScope.value().scope != bobScope.value().scope);

    REQUIRE(session.discover(alice, aliceScope.value().scope, 2).ok());
    REQUIRE(session.discover(bob, bobScope.value().scope, 2).ok());
    const AuthoringError& hidden = require_error(session.discover(alice, bobScope.value().scope, 2), AuthoringErrorCode::NotFound);
    const AuthoringError& missing = require_error(session.discover(alice, ScopeId{999}, 2), AuthoringErrorCode::NotFound);
    // Unauthorized and missing render the same code to avoid enumeration.
    CHECK(hidden.code == missing.code);
    require_error(session.discover(bob, aliceScope.value().scope, 2), AuthoringErrorCode::NotFound);
    require_error(session.discover(CallerContext{session.id(), ""}, aliceScope.value().scope, 2), AuthoringErrorCode::NotFound);
}

TEST_CASE("L1.2 schema-less, unknown, missing, unnamed, duplicate and zero grants fail the whole scope and store nothing") {
    Fixture fx;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType);
    const CallerContext alice{session.id(), "alice"};
    const auto invalid = [&](std::vector<ScopeGrant> grants) {
        require_error(session.create_scope("alice", std::move(grants), 1), AuthoringErrorCode::InvalidInput);
    };
    invalid({});
    invalid({ScopeGrant{"Raw", "office", ComponentAccessMode::Read}});
    invalid({ScopeGrant{"Nope", "office", ComponentAccessMode::Read}});
    invalid({ScopeGrant{"Light", "attic", ComponentAccessMode::Read}});
    invalid({ScopeGrant{"Light", "", ComponentAccessMode::Read}});
    invalid({ScopeGrant{"Light", "office", ComponentAccessMode::Read}, ScopeGrant{"Light", "office", ComponentAccessMode::ReadWrite}});
    invalid({ScopeGrant{"Light", "office", ComponentAccessMode::Read}, ScopeGrant{"Raw", "office", ComponentAccessMode::Read}});
    require_no_scope(session, alice, 16, 1);
}

TEST_CASE("L1.2 scope owner is a bounded nonempty UTF-8 label") {
    Fixture fx;
    AuthoringLimits limits;
    limits.maxLabelBytes = 8;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType, limits);
    for (const std::string& owner : {std::string(), std::string(9, 'o'), std::string("\xC3\x28"), std::string("a\0b", 3)}) {
        const AuthoringError& error = require_error(session.create_scope(owner, light_grant(), 1), AuthoringErrorCode::InvalidInput);
        CHECK(error.fieldPath == std::optional<std::string>("owner"));
    }
    REQUIRE(session.create_scope(std::string(8, 'o'), light_grant(), 1).ok());
}

TEST_CASE("L1.2 D2 script-control grant is rejected after a runner-successful capture, which freezes registration") {
    Fixture fx(true);
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType);
    const CallerContext alice{session.id(), "alice"};

    const AuthoringError& alone = require_error(
        session.create_scope("alice", {ScopeGrant{"Script", "main", ComponentAccessMode::Read}}, 1),
        AuthoringErrorCode::InvalidInput);
    CHECK(alone.fieldPath == std::optional<std::string>("grants[0]"));
    const AuthoringError& mixed = require_error(
        session.create_scope("alice", {light_grant().front(), ScopeGrant{"Script", "main", ComponentAccessMode::Write}}, 1),
        AuthoringErrorCode::InvalidInput);
    CHECK(mixed.fieldPath == std::optional<std::string>("grants[1]"));
    require_no_scope(session, alice, 8, 1);

    // D2: the runner capture itself succeeded, so runner registration is frozen
    // even though the session stored nothing. This is runner state, not World topology.
    CHECK_THROWS_AS(fx.runner.expose_component(fx.lateType, "Late", lua_tagged_codec<3>(), symmetric_metadata(level_schema())), std::logic_error);
    CHECK(fx.world.behavior_count() == 0);
}

TEST_CASE("L1.2 D2 a runner-failed scope capture does not freeze registration") {
    Fixture fx;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType);
    require_error(session.create_scope("alice", {ScopeGrant{"Late", "office", ComponentAccessMode::Read}}, 1), AuthoringErrorCode::InvalidInput);
    REQUIRE_NOTHROW(fx.runner.expose_component(fx.lateType, "Late", lua_tagged_codec<3>(), symmetric_metadata(level_schema())));
    REQUIRE(session.create_scope("alice", {ScopeGrant{"Late", "office", ComponentAccessMode::Read}}, 1).ok());
    CHECK_THROWS_AS(fx.runner.expose_component(fx.scriptType, "Script", lua_script_codec(), symmetric_metadata(script_schema())), std::logic_error);
}

TEST_CASE("L1.2 scope capability count and copied manifest size are bounded") {
    Fixture fx;
    AuthoringLimits oneCapability;
    oneCapability.maxScopeCapabilities = 1;
    AuthoringSession narrow(session_id('1'), fx.runtime, fx.runner, fx.scriptType, oneCapability);
    require_error(
        narrow.create_scope("alice", {light_grant().front(), ScopeGrant{"Lamp", "desk", ComponentAccessMode::Read}}, 1),
        AuthoringErrorCode::LimitExceeded);
    REQUIRE(narrow.create_scope("alice", light_grant(), 1).ok());

    AuthoringLimits tinyManifest;
    tinyManifest.maxManifestBytes = 16;
    AuthoringSession tiny(session_id('2'), fx.runtime, fx.runner, fx.scriptType, tinyManifest);
    require_error(tiny.create_scope("alice", light_grant(), 1), AuthoringErrorCode::LimitExceeded);
}

TEST_CASE("L1.2 diagnostics stay bounded for oversized grant names") {
    Fixture fx;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType);
    const std::string hugeName(20'000, 'x');
    const AuthoringError& error = require_error(
        session.create_scope("alice", {ScopeGrant{"Light", hugeName, ComponentAccessMode::Read}}, 1),
        AuthoringErrorCode::InvalidInput);
    const std::size_t total = error.diagnostic.size() + error.fieldPath.value_or("").size();
    CHECK(total <= AuthoringMaxDiagnosticBytes);
    if (error.diagnostic.find(hugeName.substr(0, 64)) != std::string::npos)
        CHECK(error.diagnostic.ends_with("..."));
}

// L1.4 Revision, revoke, removal/recreation, time and capacity (scope side)

TEST_CASE("L1.4 replace_scope increments revision even for equal grants and rejects a stale expected revision") {
    Fixture fx;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType);
    const CallerContext alice{session.id(), "alice"};
    const auto created = session.create_scope("alice", light_grant(), 1);
    REQUIRE(created.ok());
    const ScopeId scope = created.value().scope;

    const auto same = session.replace_scope(scope, created.value().revision, light_grant(), 2);
    REQUIRE(same.ok());
    CHECK(same.value().scope == scope);
    CHECK(same.value().revision == created.value().revision + 1);

    require_error(session.replace_scope(scope, created.value().revision, light_grant(), 3), AuthoringErrorCode::StaleScope);
    require_error(session.revoke_scope(scope, created.value().revision), AuthoringErrorCode::StaleScope);

    const auto narrowed = session.replace_scope(scope, same.value().revision, {ScopeGrant{"Lamp", "desk", ComponentAccessMode::Read}}, 4);
    REQUIRE(narrowed.ok());
    CHECK(narrowed.value().revision == same.value().revision + 1);
    const auto discovered = session.discover(alice, scope, 5);
    REQUIRE(discovered.ok());
    CHECK(discovered.value().revision == narrowed.value().revision);
    REQUIRE(discovered.value().manifest.capabilities().size() == 1);
    CHECK(discovered.value().manifest.capabilities().front().scriptTypeName == "Lamp");

    // A failed replacement keeps the current revision and grants.
    require_error(session.replace_scope(scope, narrowed.value().revision, {ScopeGrant{"Raw", "office", ComponentAccessMode::Read}}, 6), AuthoringErrorCode::InvalidInput);
    const auto afterFailure = session.discover(alice, scope, 6);
    REQUIRE(afterFailure.ok());
    CHECK(afterFailure.value().revision == narrowed.value().revision);
    CHECK(afterFailure.value().manifest.capabilities().front().scriptTypeName == "Lamp");
}

TEST_CASE("L1.4 D7 a revoked scope is erased: discover, submit, replace and revoke return NotFound") {
    Fixture fx;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType);
    const CallerContext alice{session.id(), "alice"};
    const auto created = session.create_scope("alice", light_grant(), 1);
    REQUIRE(created.ok());
    const ScopeId scope = created.value().scope;
    const ScopeRevision revision = created.value().revision;

    REQUIRE(session.revoke_scope(scope, revision).ok());
    require_error(session.discover(alice, scope, 2), AuthoringErrorCode::NotFound);
    ProposalSubmission submission;
    submission.scope = scope;
    submission.expectedScopeRevision = revision;
    submission.source = "return 1";
    require_error(session.submit(alice, submission), AuthoringErrorCode::NotFound);
    require_error(session.replace_scope(scope, revision, light_grant(), 3), AuthoringErrorCode::NotFound);
    require_error(session.revoke_scope(scope, revision), AuthoringErrorCode::NotFound);
    require_error(session.revoke_scope(ScopeId{999}, 1), AuthoringErrorCode::NotFound);
}

TEST_CASE("L1.4 D3 removed or recreated targets make discover StaleScope") {
    Fixture fx;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType);
    const CallerContext alice{session.id(), "alice"};
    const auto light = session.create_scope("alice", light_grant(), 1);
    const auto lamp = session.create_scope("alice", {ScopeGrant{"Lamp", "desk", ComponentAccessMode::Read}}, 1);
    REQUIRE(light.ok());
    REQUIRE(lamp.ok());

    // ABA: same type and name, new slot generation.
    fx.world.remove_component(fx.lightType, "office");
    fx.world.add_component(fx.lightType, "office", Light{40});
    require_error(session.discover(alice, light.value().scope, 2), AuthoringErrorCode::StaleScope);

    fx.world.remove_component(fx.lampType, "desk");
    require_error(session.discover(alice, lamp.value().scope, 2), AuthoringErrorCode::StaleScope);

    // Replacing the scope re-captures current targets and clears staleness.
    const auto refreshed = session.replace_scope(light.value().scope, light.value().revision, light_grant(), 3);
    REQUIRE(refreshed.ok());
    REQUIRE(session.discover(alice, light.value().scope, 4).ok());
}

TEST_CASE("L1.4 D3 value growth past runner limits makes discover LimitExceeded") {
    Runtime runtime;
    World& world = runtime.world();
    const auto aType = world.register_component<Note<0>>("test.NoteA", 1, note_codec<0>());
    const auto bType = world.register_component<Note<1>>("test.NoteB", 1, note_codec<1>());
    const auto scriptType = world.register_component<LuaBehaviorScript>("liquid.LuaBehaviorScript", 1, lua_behavior_script_codec());
    world.add_component(aType, "a", Note<0>{"short"});
    world.add_component(bType, "b", Note<1>{"short"});
    const BehaviorId writer = world.create_behavior();
    world.grant_component_access(aType, writer, "a", ComponentAccessMode::ReadWrite);
    world.grant_component_access(bType, writer, "b", ComponentAccessMode::ReadWrite);

    LuaExecutionLimits limits;
    limits.maxBufferedValueBytes = 1'500;
    LuaBehaviorRunner runner(limits);
    runner.expose_component(aType, "NoteA", lua_note_codec<0>(), symmetric_metadata(note_schema()));
    runner.expose_component(bType, "NoteB", lua_note_codec<1>(), symmetric_metadata(note_schema()));

    AuthoringSession session(session_id(), runtime, runner, scriptType);
    const CallerContext alice{session.id(), "alice"};
    const auto created = session.create_scope(
        "alice",
        {ScopeGrant{"NoteA", "a", ComponentAccessMode::Read}, ScopeGrant{"NoteB", "b", ComponentAccessMode::Read}},
        1);
    REQUIRE(created.ok());

    world.replace_component(aType, writer, "a", Note<0>{std::string(1'000, 'a')});
    world.replace_component(bType, writer, "b", Note<1>{std::string(1'000, 'b')});
    require_error(session.discover(alice, created.value().scope, 2), AuthoringErrorCode::LimitExceeded);
}

TEST_CASE("L1.4 D3 other capture failures make discover HostError") {
    Runtime runtime;
    World& world = runtime.world();
    const auto lightType = world.register_component<Light>("test.Light", 1, tagged_codec<0>());
    const auto scriptType = world.register_component<LuaBehaviorScript>("liquid.LuaBehaviorScript", 1, lua_behavior_script_codec());
    world.add_component(lightType, "office", Light{40});
    int mode = 0;
    LuaBehaviorRunner runner;
    runner.expose_component(lightType, "Light", throwing_codec<0>(mode), symmetric_metadata(level_schema()));

    AuthoringSession session(session_id(), runtime, runner, scriptType);
    const CallerContext alice{session.id(), "alice"};
    const auto created = session.create_scope("alice", light_grant(ComponentAccessMode::Read), 1);
    REQUIRE(created.ok());
    mode = 1;
    require_error(session.discover(alice, created.value().scope, 2), AuthoringErrorCode::HostError);
    mode = 0;
    REQUIRE(session.discover(alice, created.value().scope, 3).ok());
}

TEST_CASE("L1.4 discover returns copied current values and capture time without a revision change") {
    Fixture fx;
    const BehaviorId writer = fx.world.create_behavior();
    fx.world.grant_component_access(fx.lightType, writer, "office", ComponentAccessMode::ReadWrite);
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType);
    const CallerContext alice{session.id(), "alice"};
    const auto created = session.create_scope("alice", light_grant(), 10);
    REQUIRE(created.ok());
    CHECK(created.value().manifest.now_ms() == 10);
    CHECK(read_level(created.value().manifest.capabilities().front()) == 40);

    fx.world.replace_component(fx.lightType, writer, "office", Light{90});
    const auto first = session.discover(alice, created.value().scope, 20);
    REQUIRE(first.ok());
    CHECK(first.value().revision == created.value().revision);
    CHECK(first.value().manifest.now_ms() == 20);
    CHECK(read_level(first.value().manifest.capabilities().front()) == 90);

    // The returned view is a copy: the caller cannot change the stored scope.
    ScopeView copy = first.value();
    copy.revision += 5;
    fx.world.replace_component(fx.lightType, writer, "office", Light{15});
    const auto second = session.discover(alice, created.value().scope, 20);
    REQUIRE(second.ok());
    CHECK(second.value().revision == created.value().revision);
    CHECK(read_level(second.value().manifest.capabilities().front()) == 15);
    CHECK(read_level(created.value().manifest.capabilities().front()) == 40);
}

TEST_CASE("L1.4 backward or out-of-range host time is InvalidInput and advances no counter") {
    Fixture fx;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType);
    const CallerContext alice{session.id(), "alice"};
    const auto first = session.create_scope("alice", light_grant(), 100);
    REQUIRE(first.ok());

    require_error(session.create_scope("alice", light_grant(), 99), AuthoringErrorCode::InvalidInput);
    require_error(session.discover(alice, first.value().scope, 50), AuthoringErrorCode::InvalidInput);
    require_error(session.replace_scope(first.value().scope, first.value().revision, light_grant(), 1), AuthoringErrorCode::InvalidInput);
    const IntentTime tooLate = static_cast<IntentTime>(std::numeric_limits<std::int64_t>::max()) + 1;
    require_error(session.create_scope("alice", light_grant(), tooLate), AuthoringErrorCode::InvalidInput);

    const auto second = session.create_scope("alice", light_grant(), 100);
    REQUIRE(second.ok());
    CHECK(second.value().scope.value() == first.value().scope.value() + 1);
    const auto current = session.discover(alice, first.value().scope, 100);
    REQUIRE(current.ok());
    CHECK(current.value().revision == first.value().revision);
}

TEST_CASE("L1.4 active-scope capacity rejects new scopes without evicting old ones") {
    Fixture fx;
    AuthoringLimits limits;
    limits.maxActiveScopes = 2;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType, limits);
    const CallerContext alice{session.id(), "alice"};
    const auto a = session.create_scope("alice", light_grant(), 1);
    const auto b = session.create_scope("alice", light_grant(), 1);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    require_error(session.create_scope("alice", light_grant(), 1), AuthoringErrorCode::LimitExceeded);
    REQUIRE(session.discover(alice, a.value().scope, 2).ok());
    REQUIRE(session.discover(alice, b.value().scope, 2).ok());

    // Revocation frees an active slot; IDs are never reused.
    REQUIRE(session.revoke_scope(a.value().scope, a.value().revision).ok());
    const auto c = session.create_scope("alice", light_grant(), 3);
    REQUIRE(c.ok());
    CHECK(c.value().scope != a.value().scope);
    CHECK(c.value().scope > b.value().scope);
}

TEST_CASE("L1.4 scope id counter exhaustion is LimitExceeded and never wraps or reuses") {
    Fixture fx;
    AuthoringLimits limits;
    limits.maxIdValue = 2;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType, limits);
    const CallerContext alice{session.id(), "alice"};
    const auto a = session.create_scope("alice", light_grant(), 1);
    const auto b = session.create_scope("alice", light_grant(), 1);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(a.value().scope.value() == 1);
    CHECK(b.value().scope.value() == 2);
    require_error(session.create_scope("alice", light_grant(), 1), AuthoringErrorCode::LimitExceeded);
    REQUIRE(session.revoke_scope(a.value().scope, a.value().revision).ok());
    require_error(session.create_scope("alice", light_grant(), 1), AuthoringErrorCode::LimitExceeded);
    REQUIRE(session.discover(alice, b.value().scope, 2).ok());
}

TEST_CASE("L1.4 cross-session caller context is rejected as NotFound") {
    Fixture fx;
    AuthoringSession first(session_id('1'), fx.runtime, fx.runner, fx.scriptType);
    AuthoringSession second(session_id('2'), fx.runtime, fx.runner, fx.scriptType);
    const auto scope = first.create_scope("alice", light_grant(), 1);
    REQUIRE(scope.ok());

    require_error(first.discover(CallerContext{second.id(), "alice"}, scope.value().scope, 2), AuthoringErrorCode::NotFound);
    require_error(second.discover(CallerContext{second.id(), "alice"}, scope.value().scope, 2), AuthoringErrorCode::NotFound);
    require_error(second.discover(CallerContext{first.id(), "alice"}, scope.value().scope, 2), AuthoringErrorCode::NotFound);
    REQUIRE(first.discover(CallerContext{first.id(), "alice"}, scope.value().scope, 2).ok());
}

// Review cycle 1 regressions

TEST_CASE("review B1 script type validation") {
    Fixture fx(true);
    auto stale = fx.scriptType;
    stale.generation = 2;
    CHECK_THROWS_AS(AuthoringSession(session_id(), fx.runtime, fx.runner, stale), std::invalid_argument);
    auto wrong = fx.scriptType;
    wrong.id = fx.lightType.id;
    CHECK_THROWS_AS(AuthoringSession(session_id(), fx.runtime, fx.runner, wrong), std::invalid_argument);
    auto missing = fx.scriptType;
    missing.id = static_cast<ComponentTypeId>(MaxComponentTypes);
    CHECK_THROWS_AS(AuthoringSession(session_id(), fx.runtime, fx.runner, missing), std::invalid_argument);
    CHECK_NOTHROW(AuthoringSession(session_id(), fx.runtime, fx.runner, fx.scriptType));
}

TEST_CASE("review B2 complete manifest budget") {
    Fixture fx;
    const auto grants = light_grant(ComponentAccessMode::Write);
    const auto captured = fx.runner.scope_manifest(fx.world, grants, 1);
    REQUIRE(captured.ok());
    const auto& manifest = captured.manifest();
    const auto& capability = manifest.capabilities().front();
    std::size_t payload = manifest.authoring_contract().size();
    for (const auto& note : manifest.notes())
        payload += note.size();
    payload += capability.scriptTypeName.size() + capability.componentName.size()
        + capability.accessExpression.size() + capability.description.size()
        + capability.writeSchema->logical_bytes();
    AuthoringLimits limits;
    limits.maxManifestBytes = payload;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType, limits);
    require_error(session.create_scope("alice", grants, 1), AuthoringErrorCode::LimitExceeded);
}

TEST_CASE("review B2 manifest budget boundary is the L0 logical size") {
    Fixture fx;
    const auto grants = light_grant();
    const auto captured = fx.runner.scope_manifest(fx.world, grants, 1);
    REQUIRE(captured.ok());
    const std::size_t logical = captured.manifest().logical_bytes();
    REQUIRE(logical > 0);

    AuthoringLimits exact;
    exact.maxManifestBytes = logical;
    AuthoringSession admitted(session_id('1'), fx.runtime, fx.runner, fx.scriptType, exact);
    const auto created = admitted.create_scope("alice", grants, 1);
    REQUIRE(created.ok());
    CHECK(created.value().manifest.logical_bytes() == logical);
    REQUIRE(admitted.discover(CallerContext{admitted.id(), "alice"}, created.value().scope, 2).ok());

    AuthoringLimits below;
    below.maxManifestBytes = logical - 1;
    AuthoringSession rejected(session_id('2'), fx.runtime, fx.runner, fx.scriptType, below);
    require_error(rejected.create_scope("alice", grants, 1), AuthoringErrorCode::LimitExceeded);
}

TEST_CASE("review B3 checked id and revision issuance") {
    Fixture fx;
    AuthoringLimits limits;
    limits.maxIdValue = 2;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType, limits);
    const CallerContext alice{session.id(), "alice"};
    require_error(session.create_scope("alice", {}, 1), AuthoringErrorCode::InvalidInput);
    const auto a = session.create_scope("alice", light_grant(), 1);
    const auto b = session.create_scope("alice", light_grant(ComponentAccessMode::Read), 2);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(a.value().scope.value() == 1);
    CHECK(b.value().scope.value() == 2);
    for (int attempt = 0; attempt < 3; ++attempt)
        require_error(session.create_scope("alice", light_grant(), 3), AuthoringErrorCode::LimitExceeded);

    // Exhausted ids leave existing scopes and revisions untouched.
    const auto view = session.discover(alice, b.value().scope, 3);
    REQUIRE(view.ok());
    CHECK(view.value().revision == 1);
    CHECK(view.value().manifest.capabilities().front().mode == ComponentAccessMode::Read);
    const auto replaced = session.replace_scope(b.value().scope, 1, light_grant(), 4);
    REQUIRE(replaced.ok());
    CHECK(replaced.value().scope.value() == 2);
    CHECK(replaced.value().revision == 2);
    REQUIRE(session.revoke_scope(a.value().scope, 1).ok());
    require_error(session.create_scope("alice", light_grant(), 5), AuthoringErrorCode::LimitExceeded);
    require_error(session.discover(alice, ScopeId{}, 5), AuthoringErrorCode::NotFound);
    require_error(session.discover(alice, a.value().scope, 5), AuthoringErrorCode::NotFound);
}

TEST_CASE("review B5 name admission before encoder") {
    Fixture fx;
    const std::string name(257, 'x');
    fx.world.add_component(fx.fanType, name, Fan{1});
    const std::vector<LuaScopeGrant> grants{{"Fan", name, ComponentAccessMode::Read}};
    const auto result = fx.runner.scope_manifest(fx.world, grants, 1);
    require_manifest_error(result, LuaManifestErrorCode::LimitExceeded);
    CHECK(fx.fanEncodes == 0);

    const std::vector<LuaScopeGrant> longType{{std::string(257, 'y'), "office", ComponentAccessMode::Read}};
    require_manifest_error(fx.runner.scope_manifest(fx.world, longType, 1), LuaManifestErrorCode::LimitExceeded);
    CHECK(fx.fanEncodes == 0);
}
