#include "liquid/Runtime.hpp"
#include "liquid/authoring/AuthoringSession.hpp"
#include "liquid/authoring/Types.hpp"
#include "liquid/scripting/LuaCapabilityManifest.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
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

template <int Tag>
ComponentCodec<Tagged<Tag>> tagged_codec() {
    return {
        [](const Tagged<Tag>& value) { return Value{value.level}; },
        [](const Value& value) { return Tagged<Tag>{value.as_signed_integer()}; }
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

Schema level_schema() {
    return Schema::object({{"level", Schema::integer(0, 100), true}});
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

struct Fixture {
    Runtime runtime;
    World& world;
    ComponentType<Light> lightType;
    ComponentType<LuaBehaviorScript> scriptType;
    BehaviorId writer;
    std::size_t encodes = 0;
    std::size_t decodes = 0;
    LuaBehaviorRunner runner;

    explicit Fixture(LuaExecutionLimits limits = {})
        : world(runtime.world()),
          lightType(world.register_component<Light>("test.Light", 1, tagged_codec<0>())),
          scriptType(world.register_component<LuaBehaviorScript>(
              "liquid.LuaBehaviorScript", 1, lua_behavior_script_codec())),
          runner(limits) {
        world.add_component(lightType, "office", Light{40});
        writer = world.create_behavior();
        world.grant_component_access(lightType, writer, "office", ComponentAccessMode::ReadWrite);
        runner.expose_component(lightType, "Light", counting_codec<0>(encodes, decodes), symmetric_metadata(level_schema(), "Brightness"));
        world.clear_topology_mutations();
    }

    std::int64_t level() {
        const Light* light = world.get_component_named(lightType, "office");
        REQUIRE(light != nullptr);
        return light->level;
    }
};

std::vector<ScopeGrant> light_grant() {
    return {ScopeGrant{"Light", "office", ComponentAccessMode::ReadWrite}};
}

ProposalSubmission submission_for(const ScopeView& scope, std::string source, std::string rationale = {}) {
    ProposalSubmission submission;
    submission.scope = scope.scope;
    submission.expectedScopeRevision = scope.revision;
    submission.source = std::move(source);
    submission.rationale = std::move(rationale);
    return submission;
}

ScopeView create_alice_scope(AuthoringSession& session) {
    const auto created = session.create_scope("alice", light_grant(), 10);
    REQUIRE(created.ok());
    return created.value();
}

// One session with one scope owned by "alice".
struct Authoring {
    AuthoringSession session;
    CallerContext alice;
    ScopeView scope;

    explicit Authoring(Fixture& fixture, AuthoringLimits limits = {}, char digit = '0')
        : session(session_id(digit), fixture.runtime, fixture.runner, fixture.scriptType, limits),
          alice{session.id(), "alice"},
          scope(create_alice_scope(session)) {}

    AuthoringResult<ProposalId> submit(std::string source, std::string rationale = {}) {
        return session.submit(alice, submission_for(scope, std::move(source), std::move(rationale)));
    }
};

void require_no_proposal(const Authoring& authoring, std::uint64_t upTo) {
    for (std::uint64_t raw = 1; raw <= upTo; ++raw)
        require_error(authoring.session.proposal(authoring.alice, ProposalId{raw}), AuthoringErrorCode::NotFound);
}

}

// L1.3 Immutable proposal admission

TEST_CASE("L1.3 proposal records round-trip exact source bytes and the public record fields") {
    Fixture fx;
    Authoring authoring(fx);
    for (const std::string& source : {
             std::string("return 1"),
             std::string("local x = 1\r\nreturn x  \r\n"),
             std::string("  -- leading and trailing whitespace \t\n\n"),
             std::string("-- ünïcødé ✓ 光 🙂\nreturn 'ok'\n"),
             std::string("access.Light.office.propose({ value = { level = 70 } })\n")}) {
        const auto id = authoring.submit(source, "because");
        REQUIRE(id.ok());
        CHECK(id.value().valid());
        const auto record = authoring.session.proposal(authoring.alice, id.value());
        REQUIRE(record.ok());
        const BehaviorProposal& proposal = record.value();
        CHECK(proposal.id == id.value());
        CHECK(proposal.session == authoring.session.id());
        CHECK(proposal.scope == authoring.scope.scope);
        CHECK(proposal.scopeRevision == authoring.scope.revision);
        CHECK(proposal.contractVersion == LuaAuthoringContract);
        CHECK(proposal.source == source);
        CHECK(proposal.source.size() == source.size());
        CHECK(proposal.rationale == "because");
        CHECK_FALSE(proposal.managed.has_value());
    }
}

TEST_CASE("L1.3 source must be nonempty UTF-8 without NUL") {
    Fixture fx;
    Authoring authoring(fx);
    for (const std::string& source : {
             std::string(),
             std::string("return 1\0", 9),
             std::string("\0", 1),
             std::string("\xC3\x28"),
             std::string("\xC0\xAF"),
             std::string("\xED\xA0\x80"),
             std::string("\xE2\x82"),
             std::string("\xF4\x90\x80\x80"),
             std::string("\xFF"),
             std::string("return '\x80'")}) {
        const AuthoringError& error = require_error(authoring.submit(source), AuthoringErrorCode::InvalidInput);
        CHECK(error.fieldPath == std::optional<std::string>("source"));
    }
    require_no_proposal(authoring, 16);
}

TEST_CASE("L1.3 source is bounded by the smaller of the session and runner limits") {
    SECTION("session limit is smaller") {
        Fixture fx;
        AuthoringLimits limits;
        limits.maxSourceBytes = 8;
        Authoring authoring(fx, limits);
        REQUIRE(authoring.submit(std::string(8, 'x')).ok());
        const AuthoringError& error = require_error(authoring.submit(std::string(9, 'x')), AuthoringErrorCode::InvalidInput);
        CHECK(error.fieldPath == std::optional<std::string>("source"));
    }
    SECTION("runner limit is smaller") {
        LuaExecutionLimits runnerLimits;
        runnerLimits.maxSourceBytes = 6;
        Fixture fx(runnerLimits);
        Authoring authoring(fx);
        REQUIRE(authoring.submit(std::string(6, 'x')).ok());
        const AuthoringError& error = require_error(authoring.submit(std::string(7, 'x')), AuthoringErrorCode::InvalidInput);
        CHECK(error.fieldPath == std::optional<std::string>("source"));
    }
}

TEST_CASE("L1.3 rationale is optional, bounded and UTF-8") {
    Fixture fx;
    AuthoringLimits limits;
    limits.maxRationaleBytes = 4;
    Authoring authoring(fx, limits);
    REQUIRE(authoring.submit("return 1", "").ok());
    REQUIRE(authoring.submit("return 1", "abcd").ok());
    const AuthoringError& tooLong = require_error(authoring.submit("return 1", "abcde"), AuthoringErrorCode::InvalidInput);
    CHECK(tooLong.fieldPath == std::optional<std::string>("rationale"));
    const AuthoringError& badUtf8 = require_error(authoring.submit("return 1", "\xC3\x28"), AuthoringErrorCode::InvalidInput);
    CHECK(badUtf8.fieldPath == std::optional<std::string>("rationale"));
}

TEST_CASE("L1.3 D1 typed submission makes unknown and duplicate fields unrepresentable at the boundary") {
    // D1: ProposalSubmission and ScopeGrant are closed C++ structs. There is no
    // key/value map, so an unknown field or a second copy of a field cannot be
    // expressed; this case builds the submission field by field to document it.
    // Duplicate logical keys (grants) are covered by the scope tests.
    Fixture fx;
    Authoring authoring(fx);
    ProposalSubmission submission;
    submission.scope = authoring.scope.scope;
    submission.expectedScopeRevision = authoring.scope.revision;
    submission.source = "return 1";
    submission.rationale = "field by field";
    submission.managedBehavior = std::nullopt;
    submission.expectedManagedRevision = std::nullopt;
    const auto id = authoring.session.submit(authoring.alice, submission);
    REQUIRE(id.ok());
    const auto record = authoring.session.proposal(authoring.alice, id.value());
    REQUIRE(record.ok());
    CHECK(record.value().rationale == "field by field");

    // Required fields: a zero scope or a missing (zero) expected revision is invalid input or not found, never a default.
    ProposalSubmission noScope = submission;
    noScope.scope = ScopeId{};
    require_error(authoring.session.submit(authoring.alice, noScope), AuthoringErrorCode::NotFound);
    ProposalSubmission noRevision = submission;
    noRevision.expectedScopeRevision = 0;
    require_error(authoring.session.submit(authoring.alice, noRevision), AuthoringErrorCode::StaleScope);
}

TEST_CASE("L1.3 half a managed pair is InvalidInput and a full pair is NotFound until L5") {
    Fixture fx;
    Authoring authoring(fx);
    ProposalSubmission behaviorOnly = submission_for(authoring.scope, "return 1");
    behaviorOnly.managedBehavior = ManagedBehaviorId{1};
    require_error(authoring.session.submit(authoring.alice, behaviorOnly), AuthoringErrorCode::InvalidInput);

    ProposalSubmission revisionOnly = submission_for(authoring.scope, "return 1");
    revisionOnly.expectedManagedRevision = 1;
    require_error(authoring.session.submit(authoring.alice, revisionOnly), AuthoringErrorCode::InvalidInput);

    ProposalSubmission full = submission_for(authoring.scope, "return 1");
    full.managedBehavior = ManagedBehaviorId{1};
    full.expectedManagedRevision = 1;
    require_error(authoring.session.submit(authoring.alice, full), AuthoringErrorCode::NotFound);
    require_no_proposal(authoring, 8);
}

TEST_CASE("L1.3 submission never executes source or mutates World") {
    Fixture fx;
    Authoring authoring(fx);
    const std::size_t behaviors = fx.world.behavior_count();
    const std::size_t systems = fx.world.system_count();
    const std::size_t decodesBefore = fx.decodes;
    for (const std::string& source : {
             std::string("error('boom')"),
             std::string("access.Light.office.propose({ value = { level = 70 } })"),
             std::string("while true do end"),
             std::string("this is not ( valid lua"),
             std::string("access.Nope.missing.propose({ value = 1 })")}) {
        REQUIRE(authoring.submit(source).ok());
    }
    CHECK(fx.world.behavior_count() == behaviors);
    CHECK(fx.world.system_count() == systems);
    CHECK(fx.world.live_intent_ids().empty());
    CHECK(fx.world.intent_count(fx.writer) == 0);
    CHECK(fx.world.topology_mutations().empty());
    CHECK(fx.decodes == decodesBefore);
    CHECK(fx.level() == 40);
}

TEST_CASE("L1.3 a repair is a new distinct proposal and the original record is unchanged") {
    Fixture fx;
    Authoring authoring(fx);
    const auto original = authoring.submit("return 1", "first");
    const auto repair = authoring.submit("return 2", "repair");
    REQUIRE(original.ok());
    REQUIRE(repair.ok());
    CHECK(original.value() != repair.value());
    CHECK(repair.value() > original.value());

    auto copy = authoring.session.proposal(authoring.alice, original.value());
    REQUIRE(copy.ok());
    BehaviorProposal edited = copy.value();
    edited.source = "return 3";
    edited.rationale = "edited";
    const auto again = authoring.session.proposal(authoring.alice, original.value());
    REQUIRE(again.ok());
    CHECK(again.value().source == "return 1");
    CHECK(again.value().rationale == "first");
    const auto repaired = authoring.session.proposal(authoring.alice, repair.value());
    REQUIRE(repaired.ok());
    CHECK(repaired.value().source == "return 2");
}

// L1.4 Revision, removal/recreation, capacity and cross-session (proposal side)

TEST_CASE("L1.4 submit with a stale expected scope revision is StaleScope and older records survive") {
    Fixture fx;
    Authoring authoring(fx);
    const auto before = authoring.submit("return 1");
    REQUIRE(before.ok());

    const auto replaced = authoring.session.replace_scope(authoring.scope.scope, authoring.scope.revision, light_grant(), 11);
    REQUIRE(replaced.ok());
    require_error(authoring.submit("return 2"), AuthoringErrorCode::StaleScope);

    const auto after = authoring.session.submit(authoring.alice, submission_for(replaced.value(), "return 2"));
    REQUIRE(after.ok());
    const auto newRecord = authoring.session.proposal(authoring.alice, after.value());
    REQUIRE(newRecord.ok());
    CHECK(newRecord.value().scopeRevision == replaced.value().revision);
    const auto oldRecord = authoring.session.proposal(authoring.alice, before.value());
    REQUIRE(oldRecord.ok());
    CHECK(oldRecord.value().scopeRevision == authoring.scope.revision);
}

TEST_CASE("L1.4 submit re-resolves targets: recreated or removed targets are StaleScope") {
    SECTION("ABA recreation under the same name") {
        Fixture fx;
        Authoring authoring(fx);
        fx.world.remove_component(fx.lightType, "office");
        fx.world.add_component(fx.lightType, "office", Light{40});
        require_error(authoring.submit("return 1"), AuthoringErrorCode::StaleScope);
        require_no_proposal(authoring, 4);
    }
    SECTION("removed target") {
        Fixture fx;
        Authoring authoring(fx);
        fx.world.remove_component(fx.lightType, "office");
        require_error(authoring.submit("return 1"), AuthoringErrorCode::StaleScope);
        require_no_proposal(authoring, 4);
    }
}

TEST_CASE("L1.4 changed live values do not invalidate a scope for submission") {
    Fixture fx;
    Authoring authoring(fx);
    fx.world.replace_component(fx.lightType, fx.writer, "office", Light{99});
    const auto id = authoring.submit("return 1");
    REQUIRE(id.ok());
    const auto record = authoring.session.proposal(authoring.alice, id.value());
    REQUIRE(record.ok());
    CHECK(record.value().scopeRevision == authoring.scope.revision);
}

TEST_CASE("L1.4 proposal capacity rejects new records without evicting old ones") {
    Fixture fx;
    AuthoringLimits limits;
    limits.maxProposals = 2;
    Authoring authoring(fx, limits);
    const auto a = authoring.submit("return 1");
    const auto b = authoring.submit("return 2");
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    require_error(authoring.submit("return 3"), AuthoringErrorCode::LimitExceeded);
    REQUIRE(authoring.session.proposal(authoring.alice, a.value()).ok());
    REQUIRE(authoring.session.proposal(authoring.alice, b.value()).ok());
}

TEST_CASE("L1.4 proposal id counter exhaustion is LimitExceeded and never wraps") {
    Fixture fx;
    AuthoringLimits limits;
    limits.maxIdValue = 2;
    Authoring authoring(fx, limits);
    const auto a = authoring.submit("return 1");
    const auto b = authoring.submit("return 2");
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(a.value().value() == 1);
    CHECK(b.value().value() == 2);
    require_error(authoring.submit("return 3"), AuthoringErrorCode::LimitExceeded);
    REQUIRE(authoring.session.proposal(authoring.alice, a.value()).ok());
}

TEST_CASE("L1.4 proposal lookup and submit enforce caller and session ownership") {
    Fixture fx;
    Authoring first(fx, {}, '1');
    Authoring second(fx, {}, '2');
    const auto id = first.submit("return 1");
    REQUIRE(id.ok());

    const CallerContext bob{first.session.id(), "bob"};
    require_error(first.session.proposal(bob, id.value()), AuthoringErrorCode::NotFound);
    require_error(first.session.proposal(second.alice, id.value()), AuthoringErrorCode::NotFound);
    require_error(second.session.proposal(second.alice, id.value()), AuthoringErrorCode::NotFound);
    require_error(first.session.proposal(first.alice, ProposalId{999}), AuthoringErrorCode::NotFound);
    require_error(first.session.proposal(first.alice, ProposalId{}), AuthoringErrorCode::NotFound);

    require_error(first.session.submit(bob, submission_for(first.scope, "return 1")), AuthoringErrorCode::NotFound);
    require_error(first.session.submit(second.alice, submission_for(first.scope, "return 1")), AuthoringErrorCode::NotFound);
    REQUIRE(first.session.proposal(first.alice, id.value()).ok());
}

TEST_CASE("L1.4 submit to a revoked scope is NotFound and creates no record") {
    Fixture fx;
    Authoring authoring(fx);
    REQUIRE(authoring.session.revoke_scope(authoring.scope.scope, authoring.scope.revision).ok());
    require_error(authoring.submit("return 1"), AuthoringErrorCode::NotFound);
    require_no_proposal(authoring, 4);
}

// Review cycle 1 regressions

TEST_CASE("review B4 ABA with invalid recreated value") {
    Fixture fx;
    Authoring authoring(fx);
    fx.world.remove_component(fx.lightType, "office");
    fx.world.add_component(fx.lightType, "office", Light{101});
    require_error(authoring.session.discover(authoring.alice, authoring.scope.scope, 11), AuthoringErrorCode::StaleScope);
    require_error(authoring.submit("return 1"), AuthoringErrorCode::StaleScope);
    require_no_proposal(authoring, 2);
}

TEST_CASE("review B3 checked proposal id issuance") {
    Fixture fx;
    AuthoringLimits limits;
    limits.maxIdValue = 2;
    Authoring authoring(fx, limits);
    require_error(authoring.submit(""), AuthoringErrorCode::InvalidInput);
    const auto a = authoring.submit("return 1");
    const auto b = authoring.submit("return 2");
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(a.value().value() == 1);
    CHECK(b.value().value() == 2);
    for (int attempt = 0; attempt < 3; ++attempt)
        require_error(authoring.submit("return 3"), AuthoringErrorCode::LimitExceeded);
    require_error(authoring.session.proposal(authoring.alice, ProposalId{}), AuthoringErrorCode::NotFound);
    require_error(authoring.session.proposal(authoring.alice, ProposalId{3}), AuthoringErrorCode::NotFound);
    const auto second = authoring.session.proposal(authoring.alice, b.value());
    REQUIRE(second.ok());
    CHECK(second.value().source == "return 2");
}
