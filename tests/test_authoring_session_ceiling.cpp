#include "liquid/Runtime.hpp"
#include "liquid/authoring/AuthoringSession.hpp"
#include "liquid/authoring/Types.hpp"
#include "liquid/scripting/LuaCapabilityManifest.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
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

// World with described Light and Fan bindings named "office".
struct Fixture {
    Runtime runtime;
    World& world;
    ComponentType<Light> lightType;
    ComponentType<Fan> fanType;
    ComponentType<LuaBehaviorScript> scriptType;
    LuaBehaviorRunner runner;

    Fixture()
        : world(runtime.world()),
          lightType(world.register_component<Light>("test.Light", 1, tagged_codec<0>())),
          fanType(world.register_component<Fan>("test.Fan", 1, tagged_codec<1>())),
          scriptType(world.register_component<LuaBehaviorScript>(
              "liquid.LuaBehaviorScript", 1, lua_behavior_script_codec())) {
        world.add_component(lightType, "office", Light{40});
        world.add_component(fanType, "office", Fan{10});
        runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema(), "Brightness"));
        runner.expose_component(fanType, "Fan", lua_tagged_codec<1>(), symmetric_metadata(level_schema(), "Fan speed"));
        world.clear_topology_mutations();
    }
};

std::vector<ScopeGrant> light_grant() {
    return {ScopeGrant{"Light", "office", ComponentAccessMode::ReadWrite}};
}

std::vector<ScopeGrant> fan_and_light_grants() {
    return {
        ScopeGrant{"Fan", "office", ComponentAccessMode::ReadWrite},
        ScopeGrant{"Light", "office", ComponentAccessMode::ReadWrite}
    };
}

ProposalSubmission submission_for(const ScopeView& scope, std::string source, std::string rationale = {}) {
    ProposalSubmission submission;
    submission.scope = scope.scope;
    submission.expectedScopeRevision = scope.revision;
    submission.source = std::move(source);
    submission.rationale = std::move(rationale);
    return submission;
}

// L0 logical size of the manifest a scope over `grants` is admitted with.
std::size_t manifest_bytes(Fixture& fx, const std::vector<ScopeGrant>& grants) {
    const LuaManifestResult captured = fx.runner.scope_manifest(fx.world, grants, 1);
    REQUIRE(captured.ok());
    return captured.manifest().logical_bytes();
}

// Independent restatement of the documented charge rule (contract logical
// payload accounting): 8 per numeric scalar, ID or enum; 1 per boolean or
// optional flag; string byte length; 8 per collection element or record field.
constexpr std::size_t Scalar = 8;
constexpr std::size_t Flag = 1;
constexpr std::size_t Element = 8;

// Private capture: 7 fields + 5 scalars + LuaExecutionLimits (12 fields,
// 11 size scalars, 1 flag) = 56 + 40 + 96 + 88 + 1 = 281; each entry is one
// element of 3 scalar fields = 8 + 3 * (8 + 8) = 56.
std::size_t capture_charge(std::size_t entries) {
    return 7 * Element + 5 * Scalar + 12 * Element + 11 * Scalar + Flag
        + entries * (Element + 3 * (Element + Scalar));
}

// Scope record: 5 fields + revision scalar = 48; owner bytes; each grant
// 8 + 3 * 8 + mode 8 + name bytes; each target 8 + 2 * (8 + 8) = 40; the
// capture (one entry per grant); the manifest's own logical bytes.
std::size_t scope_charge(std::string_view owner, const std::vector<ScopeGrant>& grants, std::size_t manifest) {
    std::size_t bytes = 5 * Element + Scalar + owner.size();
    for (const ScopeGrant& grant : grants)
        bytes += Element + 3 * Element + Scalar + grant.scriptTypeName.size() + grant.componentName.size();
    bytes += grants.size() * (Element + 2 * (Element + Scalar));
    return bytes + capture_charge(grants.size()) + manifest;
}

// Proposal record (no managed revision): (3 + 8) fields = 88; id, session,
// scope and scope revision = 32; managed flag 1; contract version bytes;
// source, rationale and owner bytes; the scope capture counted again.
std::size_t proposal_charge(std::string_view owner, std::string_view source, std::string_view rationale, std::size_t captureEntries) {
    return 11 * Element + 4 * Scalar + Flag + LuaAuthoringContract.size()
        + source.size() + rationale.size() + owner.size() + capture_charge(captureEntries);
}

}

TEST_CASE("L1-fix session payload ceiling defaults to 64 MiB", "[l1fix]") {
    CHECK(AuthoringLimits{}.maxSessionPayloadBytes == (64u << 20));
    CHECK(AuthoringLimits{}.maxSessionPayloadBytes == 67'108'864);
}

TEST_CASE("L1-fix session constructor rejects a zero or raised payload ceiling and accepts a lowered one", "[l1fix]") {
    Fixture fx;
    AuthoringLimits zero;
    zero.maxSessionPayloadBytes = 0;
    CHECK_THROWS_AS(AuthoringSession(session_id(), fx.runtime, fx.runner, fx.scriptType, zero), std::invalid_argument);
    AuthoringLimits raised;
    raised.maxSessionPayloadBytes = AuthoringLimits{}.maxSessionPayloadBytes + 1;
    CHECK_THROWS_AS(AuthoringSession(session_id(), fx.runtime, fx.runner, fx.scriptType, raised), std::invalid_argument);

    AuthoringLimits lowered;
    lowered.maxSessionPayloadBytes = 4'096;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType, lowered);
    CHECK(session.limits().maxSessionPayloadBytes == 4'096);
}

TEST_CASE("L1-fix scope admission over the session ceiling is LimitExceeded and leaves no partial state", "[l1fix]") {
    Fixture fx;
    const std::size_t scope = scope_charge("alice", light_grant(), manifest_bytes(fx, light_grant()));
    AuthoringLimits limits;
    limits.maxSessionPayloadBytes = 2 * scope - 1;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType, limits);
    const CallerContext alice{session.id(), "alice"};
    const CallerContext bobby{session.id(), "bobby"};
    const auto first = session.create_scope("alice", light_grant(), 10);
    REQUIRE(first.ok());

    // The ceiling is session-wide: no owner fits a second scope.
    require_error(session.create_scope("alice", light_grant(), 50), AuthoringErrorCode::LimitExceeded);
    require_error(session.create_scope("bobby", light_grant(), 50), AuthoringErrorCode::LimitExceeded);
    require_error(session.discover(alice, ScopeId{2}, 10), AuthoringErrorCode::NotFound);
    require_error(session.discover(bobby, ScopeId{2}, 10), AuthoringErrorCode::NotFound);
    const auto current = session.discover(alice, first.value().scope, 10);
    REQUIRE(current.ok());
    CHECK(current.value().revision == 1);

    // The failures consumed no id and did not advance the session clock to 50.
    REQUIRE(session.revoke_scope(first.value().scope, 1).ok());
    const auto later = session.create_scope("alice", light_grant(), 20);
    REQUIRE(later.ok());
    CHECK(later.value().scope.value() == 2);
}

TEST_CASE("L1-fix proposal submission over the session ceiling is LimitExceeded and leaves no partial state", "[l1fix]") {
    Fixture fx;
    const auto grants = light_grant();
    const std::size_t scope = scope_charge("alice", grants, manifest_bytes(fx, grants));
    const std::size_t proposal = proposal_charge("alice", "return 1", "", 1);
    AuthoringLimits limits;
    limits.maxSessionPayloadBytes = scope + 2 * proposal;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType, limits);
    const CallerContext alice{session.id(), "alice"};
    const auto view = session.create_scope("alice", grants, 10);
    REQUIRE(view.ok());
    const auto first = session.submit(alice, submission_for(view.value(), "return 1"));
    REQUIRE(first.ok());
    CHECK(first.value().value() == 1);

    // One byte of source or rationale past the remaining capacity fails whole.
    require_error(session.submit(alice, submission_for(view.value(), "return 22")), AuthoringErrorCode::LimitExceeded);
    require_error(session.submit(alice, submission_for(view.value(), "return 2", "r")), AuthoringErrorCode::LimitExceeded);
    require_error(session.proposal(alice, ProposalId{2}), AuthoringErrorCode::NotFound);

    // The exact remainder is admitted under the next id; then nothing fits.
    const auto second = session.submit(alice, submission_for(view.value(), "return 2"));
    REQUIRE(second.ok());
    CHECK(second.value().value() == 2);
    require_error(session.submit(alice, submission_for(view.value(), "x")), AuthoringErrorCode::LimitExceeded);
    require_error(session.proposal(alice, ProposalId{3}), AuthoringErrorCode::NotFound);

    const auto kept = session.proposal(alice, first.value());
    REQUIRE(kept.ok());
    CHECK(kept.value().source == "return 1");
    const auto current = session.discover(alice, view.value().scope, 10);
    REQUIRE(current.ok());
    CHECK(current.value().revision == 1);
}

TEST_CASE("L1-fix replace_scope that grows past the session ceiling keeps the old scope and revision", "[l1fix]") {
    Fixture fx;
    const auto small = light_grant();
    const auto large = fan_and_light_grants();
    const std::size_t smallCharge = scope_charge("alice", small, manifest_bytes(fx, small));
    const std::size_t largeCharge = scope_charge("alice", large, manifest_bytes(fx, large));
    AuthoringLimits limits;
    limits.maxSessionPayloadBytes = largeCharge - 1;
    // Room for one small scope but not two: an equal replacement succeeds
    // only if the old charge is swapped out rather than added to.
    REQUIRE(smallCharge <= limits.maxSessionPayloadBytes);
    REQUIRE(2 * smallCharge > limits.maxSessionPayloadBytes);
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType, limits);
    const CallerContext alice{session.id(), "alice"};
    const auto created = session.create_scope("alice", small, 10);
    REQUIRE(created.ok());

    require_error(session.replace_scope(created.value().scope, 1, large, 50), AuthoringErrorCode::LimitExceeded);

    // Old grants and revision intact; the session clock did not move to 50.
    const auto kept = session.discover(alice, created.value().scope, 10);
    REQUIRE(kept.ok());
    CHECK(kept.value().revision == 1);
    REQUIRE(kept.value().manifest.capabilities().size() == 1);
    CHECK(kept.value().manifest.capabilities().front().scriptTypeName == "Light");

    const auto replaced = session.replace_scope(created.value().scope, 1, small, 20);
    REQUIRE(replaced.ok());
    CHECK(replaced.value().revision == 2);
    require_error(session.replace_scope(created.value().scope, 2, large, 30), AuthoringErrorCode::LimitExceeded);
    const auto after = session.discover(alice, created.value().scope, 30);
    REQUIRE(after.ok());
    CHECK(after.value().revision == 2);
}

TEST_CASE("L1-fix replace_scope swaps charges exactly, so shrinking frees what growing took", "[l1fix]") {
    Fixture fx;
    const auto small = light_grant();
    const auto large = fan_and_light_grants();
    const std::size_t smallCharge = scope_charge("alice", small, manifest_bytes(fx, small));
    const std::size_t largeCharge = scope_charge("alice", large, manifest_bytes(fx, large));
    REQUIRE(largeCharge > smallCharge);
    AuthoringLimits limits;
    limits.maxSessionPayloadBytes = smallCharge + largeCharge;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType, limits);
    const auto a = session.create_scope("alice", small, 10);
    const auto b = session.create_scope("alice", small, 10);
    REQUIRE(a.ok());
    REQUIRE(b.ok());

    // Growing to exactly the ceiling is admitted; then nothing else fits.
    REQUIRE(session.replace_scope(b.value().scope, 1, large, 10).ok());
    require_error(session.create_scope("alice", small, 10), AuthoringErrorCode::LimitExceeded);

    // A leaked or double-counted swap would make the second grow fail.
    REQUIRE(session.replace_scope(b.value().scope, 2, small, 10).ok());
    REQUIRE(session.replace_scope(b.value().scope, 3, large, 10).ok());
    require_error(session.replace_scope(a.value().scope, 1, large, 10), AuthoringErrorCode::LimitExceeded);
    REQUIRE(session.revoke_scope(b.value().scope, 4).ok());
    REQUIRE(session.replace_scope(a.value().scope, 1, large, 10).ok());
}

TEST_CASE("L1-fix revoke releases its scope charge while retained proposals keep theirs", "[l1fix]") {
    Fixture fx;
    const auto grants = light_grant();
    const std::size_t scope = scope_charge("alice", grants, manifest_bytes(fx, grants));
    const std::size_t proposal = proposal_charge("alice", "return 1", "", 1);
    AuthoringLimits limits;
    // Two scopes fit only if the retained proposal's charge were released.
    limits.maxSessionPayloadBytes = 2 * scope + proposal - 1;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType, limits);
    const CallerContext alice{session.id(), "alice"};
    const auto a = session.create_scope("alice", grants, 10);
    REQUIRE(a.ok());
    const auto kept = session.submit(alice, submission_for(a.value(), "return 1"));
    REQUIRE(kept.ok());
    require_error(session.create_scope("alice", grants, 10), AuthoringErrorCode::LimitExceeded);

    // Revocation releases the scope: the rejected admission now succeeds.
    REQUIRE(session.revoke_scope(a.value().scope, 1).ok());
    const auto b = session.create_scope("alice", grants, 10);
    REQUIRE(b.ok());

    // The proposal outlives its scope and keeps its charge.
    REQUIRE(session.proposal(alice, kept.value()).ok());
    require_error(session.create_scope("alice", grants, 10), AuthoringErrorCode::LimitExceeded);

    REQUIRE(session.revoke_scope(b.value().scope, 1).ok());
    REQUIRE(session.create_scope("alice", grants, 10).ok());
}

TEST_CASE("L1-fix session payload boundary admits load equal to the ceiling and rejects one byte more", "[l1fix]") {
    Fixture fx;
    const auto grants = light_grant();
    const std::size_t manifest = manifest_bytes(fx, grants);
    const std::size_t scope = scope_charge("alice", grants, manifest);
    // 48 (fields + revision) + "alice" 5 + grant (8 + 24 + 8 + "Light" 5
    // + "office" 6 = 51) + target 40 + capture (281 + 56 = 337) = 481.
    CHECK(scope == 481 + manifest);
    const std::size_t proposal = proposal_charge("alice", "return 1", "why", 1);
    // 88 + 32 + 1 + "liquid.lua.authoring/1" 22 + "alice" 5 + capture 337
    // = 485, plus source 8 and rationale 3.
    CHECK(proposal == 485 + 8 + 3);

    AuthoringLimits scopeExact;
    scopeExact.maxSessionPayloadBytes = scope;
    AuthoringSession scopeAtLimit(session_id('1'), fx.runtime, fx.runner, fx.scriptType, scopeExact);
    REQUIRE(scopeAtLimit.create_scope("alice", grants, 1).ok());

    AuthoringLimits scopeBelow;
    scopeBelow.maxSessionPayloadBytes = scope - 1;
    AuthoringSession scopeOverLimit(session_id('2'), fx.runtime, fx.runner, fx.scriptType, scopeBelow);
    require_error(scopeOverLimit.create_scope("alice", grants, 1), AuthoringErrorCode::LimitExceeded);

    AuthoringLimits proposalExact;
    proposalExact.maxSessionPayloadBytes = scope + proposal;
    AuthoringSession proposalAtLimit(session_id('3'), fx.runtime, fx.runner, fx.scriptType, proposalExact);
    const auto exactScope = proposalAtLimit.create_scope("alice", grants, 1);
    REQUIRE(exactScope.ok());
    REQUIRE(proposalAtLimit.submit(
        CallerContext{proposalAtLimit.id(), "alice"}, submission_for(exactScope.value(), "return 1", "why")).ok());

    AuthoringLimits proposalBelow;
    proposalBelow.maxSessionPayloadBytes = scope + proposal - 1;
    AuthoringSession proposalOverLimit(session_id('4'), fx.runtime, fx.runner, fx.scriptType, proposalBelow);
    const auto belowScope = proposalOverLimit.create_scope("alice", grants, 1);
    REQUIRE(belowScope.ok());
    require_error(
        proposalOverLimit.submit(CallerContext{proposalOverLimit.id(), "alice"}, submission_for(belowScope.value(), "return 1", "why")),
        AuthoringErrorCode::LimitExceeded);
}

TEST_CASE("L1-fix default session ceiling leaves the L1 capacity limits in charge", "[l1fix]") {
    Fixture fx;
    const AuthoringLimits limits;
    AuthoringSession session(session_id(), fx.runtime, fx.runner, fx.scriptType);
    const CallerContext alice{session.id(), "alice"};
    std::vector<ScopeView> views;
    for (std::size_t index = 0; index < limits.maxActiveScopes; ++index) {
        const auto created = session.create_scope("alice", light_grant(), 1);
        REQUIRE(created.ok());
        views.push_back(created.value());
    }
    require_error(session.create_scope("alice", light_grant(), 1), AuthoringErrorCode::LimitExceeded);

    // Maximum-size proposals up to the proposal capacity stay far below 64 MiB.
    const std::string source(std::min(limits.maxSourceBytes, LuaExecutionLimits{}.maxSourceBytes), 'x');
    const std::string rationale(limits.maxRationaleBytes, 'y');
    for (std::size_t index = 0; index < limits.maxProposals; ++index)
        REQUIRE(session.submit(alice, submission_for(views.front(), source, rationale)).ok());
    require_error(session.submit(alice, submission_for(views.front(), "return 1")), AuthoringErrorCode::LimitExceeded);

    const auto replaced = session.replace_scope(views.back().scope, 1, fan_and_light_grants(), 2);
    REQUIRE(replaced.ok());
    CHECK(replaced.value().revision == 2);
    REQUIRE(session.revoke_scope(views.front().scope, 1).ok());
    REQUIRE(session.create_scope("alice", light_grant(), 2).ok());
}
