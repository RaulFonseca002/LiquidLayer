#include "liquid/authoring/AuthoringSession.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace liquid::authoring {

namespace {

using scripting::LuaCapabilityManifest;
using scripting::LuaManifestCapture;
using scripting::LuaManifestError;
using scripting::LuaManifestErrorCode;
using scripting::LuaManifestResult;
using scripting::LuaScopeTarget;

constexpr std::string_view Ellipsis = "...";

// Keeps diagnostic plus field path within AuthoringMaxDiagnosticBytes.
AuthoringError make_error(
    AuthoringErrorCode code,
    std::string_view message,
    std::optional<std::string> fieldPath = std::nullopt
) {
    if (message.empty())
        message = "authoring request failed";

    const std::size_t pathBytes = fieldPath ? fieldPath->size() : 0;
    const std::size_t budget = AuthoringMaxDiagnosticBytes > pathBytes ? AuthoringMaxDiagnosticBytes - pathBytes : 0;
    std::string diagnostic;
    if (message.size() <= budget) {
        diagnostic = std::string(message);
    } else {
        std::size_t cut = budget > Ellipsis.size() ? budget - Ellipsis.size() : 0;
        // Do not split a UTF-8 sequence.
        while (cut > 0 && (static_cast<unsigned char>(message[cut]) & 0xC0) == 0x80)
            --cut;
        diagnostic = std::string(message.substr(0, cut)) + std::string(Ellipsis);
    }
    return AuthoringError{code, std::move(diagnostic), std::move(fieldPath)};
}

// shortcut: D5 — third private UTF-8 validator; the allowlist excludes the
// shared copies in src/Value.cpp and src/scripting/LuaValueSchema.cpp.
// Upgrade trigger: consolidate into one helper when src/Value.cpp and
// LuaValueSchema.cpp are in an authorized allowlist.
bool valid_utf8(std::string_view text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        if (lead < 0x80) {
            ++index;
            continue;
        }

        std::size_t length = 0;
        std::uint32_t codePoint = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
            codePoint = lead & 0x1F;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
            codePoint = lead & 0x0F;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            codePoint = lead & 0x07;
        } else {
            return false;
        }
        if (text.size() - index < length)
            return false;

        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xC0) != 0x80)
                return false;
            codePoint = (codePoint << 6) | (next & 0x3F);
        }

        if ((length == 3 && codePoint < 0x800)
            || (length == 4 && codePoint < 0x10000)
            || codePoint > 0x10FFFF
            || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
            return false;
        index += length;
    }
    return true;
}

std::optional<AuthoringError> check_text(
    std::string_view text,
    std::size_t maxBytes,
    bool required,
    const std::string& fieldPath
) {
    if (required && text.empty())
        return make_error(AuthoringErrorCode::InvalidInput, fieldPath + " must not be empty", fieldPath);
    if (text.size() > maxBytes)
        return make_error(
            AuthoringErrorCode::InvalidInput,
            fieldPath + " exceeds " + std::to_string(maxBytes) + " bytes",
            fieldPath);
    if (text.find('\0') != std::string_view::npos)
        return make_error(AuthoringErrorCode::InvalidInput, fieldPath + " contains a NUL byte", fieldPath);
    if (!valid_utf8(text))
        return make_error(AuthoringErrorCode::InvalidInput, fieldPath + " is not valid UTF-8", fieldPath);
    return std::nullopt;
}

std::string grant_path(std::size_t index) {
    return "grants[" + std::to_string(index) + "]";
}

// The one issue path for scope ids, proposal ids and scope revisions: the
// successor of `last`, or empty once `last` reaches `max`. Never wraps, so
// id 0 is never issued and no id or revision repeats.
constexpr std::optional<std::uint64_t> checked_next(std::uint64_t last, std::uint64_t max) {
    if (last >= max)
        return std::nullopt;
    return last + 1;
}

constexpr std::uint64_t MaxUint64 = std::numeric_limits<std::uint64_t>::max();
static_assert(!checked_next(MaxUint64, MaxUint64));
static_assert(checked_next(MaxUint64 - 1, MaxUint64) == MaxUint64);
static_assert(checked_next(0, MaxUint64) == 1);
static_assert(!checked_next(2, 2) && checked_next(1, 2) == 2);

bool same_entries(const LuaManifestCapture& left, const LuaManifestCapture& right) {
    const auto& a = left.entries();
    const auto& b = right.entries();
    if (left.runner_id() != right.runner_id() || a.size() != b.size())
        return false;
    for (std::size_t index = 0; index < a.size(); ++index) {
        if (a[index].bindingIndex != b[index].bindingIndex
            || a[index].type != b[index].type
            || a[index].slot != b[index].slot)
            return false;
    }
    return true;
}

bool valid_limits(const AuthoringLimits& limits) {
    const AuthoringLimits defaults;
    const std::pair<std::size_t, std::size_t> bounded[] = {
        {limits.maxActiveScopes, defaults.maxActiveScopes},
        {limits.maxProposals, defaults.maxProposals},
        {limits.maxEvaluations, defaults.maxEvaluations},
        {limits.maxManagedBehaviors, defaults.maxManagedBehaviors},
        {limits.maxSourceBytes, defaults.maxSourceBytes},
        {limits.maxRationaleBytes, defaults.maxRationaleBytes},
        {limits.maxScopeCapabilities, defaults.maxScopeCapabilities},
        {limits.maxLabelBytes, defaults.maxLabelBytes},
        {limits.maxManifestBytes, defaults.maxManifestBytes},
        {limits.maxSessionPayloadBytes, defaults.maxSessionPayloadBytes},
    };
    for (const auto& [value, ceiling] : bounded) {
        if (value == 0 || value > ceiling)
            return false;
    }
    return limits.maxIdValue != 0;
}

// Contract logical payload accounting (LIQUID_IMPLEMENTATION_CONTRACT.md):
// 8 bytes per numeric scalar, ID or enum, 1 per boolean or optional-presence
// flag, byte length for strings, and 8 per collection element or record field.
// shortcut: second private copy of the L0 accounting constants; the first is
// in src/scripting/LuaCapabilityManifest.cpp, outside this step's allowlist.
// Upgrade trigger: share one definition when that file is in an authorized
// allowlist or a third copy is needed.
constexpr std::size_t LogicalScalarBytes = 8;
constexpr std::size_t LogicalFlagBytes = 1;
constexpr std::size_t LogicalElementBytes = 8;

// LuaManifestCapture, the host-only identity outside the manifest's
// logical_bytes(): 7 fields (kind, world, behavior, access revision, runner,
// limits, entries), the first five scalars or IDs; LuaExecutionLimits is 12
// fields, 11 sizes and 1 boolean; each entry is one element of 3 scalar fields
// (binding index, type, slot).
constexpr std::size_t CaptureFields = 7;
constexpr std::size_t CaptureScalarFields = 5;
constexpr std::size_t LimitSizeFields = 11;
constexpr std::size_t LimitFlagFields = 1;
static_assert(sizeof(scripting::LuaExecutionLimits) == (LimitSizeFields + LimitFlagFields) * sizeof(std::size_t),
    "update the session payload accounting for the new LuaExecutionLimits field");
constexpr std::size_t CaptureEntryFields = 3;
constexpr std::size_t CaptureFixedBytes = CaptureFields * LogicalElementBytes
    + CaptureScalarFields * LogicalScalarBytes
    + (LimitSizeFields + LimitFlagFields) * LogicalElementBytes
    + LimitSizeFields * LogicalScalarBytes
    + LimitFlagFields * LogicalFlagBytes;
constexpr std::size_t CaptureEntryBytes = LogicalElementBytes
    + CaptureEntryFields * (LogicalElementBytes + LogicalScalarBytes);

// Saturates instead of wrapping; a saturated charge always exceeds the ceiling.
constexpr std::size_t saturating_add(std::size_t total, std::size_t bytes) {
    return bytes > std::numeric_limits<std::size_t>::max() - total
        ? std::numeric_limits<std::size_t>::max()
        : total + bytes;
}

std::size_t capture_bytes(const LuaManifestCapture& capture) {
    std::size_t bytes = CaptureFixedBytes;
    for (std::size_t entry = 0; entry < capture.entries().size(); ++entry)
        bytes = saturating_add(bytes, CaptureEntryBytes);
    return bytes;
}

}

AuthoringResult<AuthoringSessionId> AuthoringSessionId::from_hex(std::string_view text) {
    // The diagnostic never echoes the rejected text.
    if (text.size() != AuthoringSessionIdHexDigits)
        return make_error(AuthoringErrorCode::InvalidInput, "session id must be exactly 32 lowercase hexadecimal digits");
    for (char digit : text) {
        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f')))
            return make_error(AuthoringErrorCode::InvalidInput, "session id must be exactly 32 lowercase hexadecimal digits");
    }
    return AuthoringSessionId(std::string(text));
}

AuthoringSessionId::AuthoringSessionId(std::string hex)
    : digits(std::move(hex))
{
}

const std::string& AuthoringSessionId::hex() const {
    return digits;
}

struct AuthoringSession::Impl {
    struct ScopeRecord {
        std::string owner;
        std::vector<ScopeGrant> grants;
        ScopeRevision revision = 0;
        // Private target identities, runner identity and effective limits.
        LuaManifestCapture capture;
        // The same identities in grant order, compared before any value is read.
        std::vector<LuaScopeTarget> targets;
        // scope_charge() at admission, kept so release is exact; the admitted
        // manifest it covers is not retained. Bookkeeping, not payload.
        std::size_t payloadCharge = 0;
    };

    struct Admission {
        LuaCapabilityManifest manifest;
        std::vector<LuaScopeTarget> targets;
    };

    struct ProposalRecord {
        BehaviorProposal proposal;
        std::string owner;
        LuaManifestCapture capture;
    };

    enum class CaptureStage {
        Admission,
        Recapture
    };

    AuthoringSessionId id;
    AuthoringLimits limits;
    Runtime& runtime;
    scripting::LuaBehaviorRunner& runner;
    ComponentType<scripting::LuaBehaviorScript> scriptType;
    std::optional<IntentTime> lastNow;
    // Last issued ids; 0 means none has been issued.
    std::uint64_t lastScope = 0;
    std::uint64_t lastProposal = 0;
    std::map<ScopeId, ScopeRecord> scopes;
    std::map<ProposalId, ProposalRecord> proposals;
    // The one session payload counter: the sum of the charges of every
    // retained record, never above limits.maxSessionPayloadBytes.
    std::size_t sessionPayloadBytes = 0;

    // Scope record: 5 fields (owner, grants, revision, capture, targets);
    // owner bytes; each grant one element of 3 fields (two name strings and
    // the mode enum); the revision scalar; capture_bytes(); each target one
    // element of 2 ID fields (type, slot); plus the admitted manifest's own
    // logical_bytes(), duplicated data counted again.
    static std::size_t scope_charge(std::string_view owner, const std::vector<ScopeGrant>& grants, const Admission& admission) {
        constexpr std::size_t RecordFields = 5;
        constexpr std::size_t GrantFields = 3;
        constexpr std::size_t TargetFields = 2;
        std::size_t bytes = saturating_add(RecordFields * LogicalElementBytes + LogicalScalarBytes, owner.size());
        for (const ScopeGrant& grant : grants) {
            bytes = saturating_add(bytes, LogicalElementBytes + GrantFields * LogicalElementBytes + LogicalScalarBytes);
            bytes = saturating_add(bytes, grant.scriptTypeName.size());
            bytes = saturating_add(bytes, grant.componentName.size());
        }
        for (std::size_t target = 0; target < admission.targets.size(); ++target)
            bytes = saturating_add(bytes, LogicalElementBytes + TargetFields * (LogicalElementBytes + LogicalScalarBytes));
        bytes = saturating_add(bytes, capture_bytes(admission.manifest.capture()));
        return saturating_add(bytes, admission.manifest.logical_bytes());
    }

    // Proposal record: 3 fields (proposal, owner, capture). The proposal is
    // 8 fields: id, session and scope IDs and the scope revision scalar;
    // contract version, source and rationale bytes; the managed optional's
    // presence flag, plus 2 scalar fields (behavior ID, revision) when present.
    // Then owner bytes and capture_bytes() of the scope capture, counted again.
    static std::size_t proposal_charge(const ProposalRecord& record) {
        constexpr std::size_t RecordFields = 3;
        constexpr std::size_t ProposalFields = 8;
        constexpr std::size_t ProposalScalarFields = 4;
        constexpr std::size_t ManagedFields = 2;
        const BehaviorProposal& proposal = record.proposal;
        std::size_t bytes = (RecordFields + ProposalFields) * LogicalElementBytes
            + ProposalScalarFields * LogicalScalarBytes
            + LogicalFlagBytes;
        if (proposal.managed)
            bytes += ManagedFields * (LogicalElementBytes + LogicalScalarBytes);
        for (std::size_t text : {proposal.contractVersion.size(), proposal.source.size(), proposal.rationale.size(), record.owner.size()})
            bytes = saturating_add(bytes, text);
        return saturating_add(bytes, capture_bytes(record.capture));
    }

    // Whether retaining `charge` in place of `released` retained bytes stays
    // within the session ceiling. The counter never exceeds the ceiling and
    // `released` never exceeds the counter, so neither subtraction wraps.
    bool payload_fits(std::size_t released, std::size_t charge) const {
        const std::size_t retained = sessionPayloadBytes - released;
        return charge <= limits.maxSessionPayloadBytes - retained;
    }

    std::optional<AuthoringError> check_time(IntentTime now) const {
        if (now > static_cast<IntentTime>(std::numeric_limits<std::int64_t>::max()))
            return make_error(AuthoringErrorCode::InvalidInput, "time is outside the signed 64-bit range", "now");
        if (lastNow && now < *lastNow)
            return make_error(AuthoringErrorCode::InvalidInput, "time moved backwards", "now");
        return std::nullopt;
    }

    std::optional<AuthoringError> check_grants(const std::vector<ScopeGrant>& grants) const {
        if (grants.empty())
            return make_error(AuthoringErrorCode::InvalidInput, "a scope requires at least one grant", "grants");
        if (grants.size() > limits.maxScopeCapabilities)
            return make_error(AuthoringErrorCode::LimitExceeded, "scope exceeds the grant limit", "grants");
        for (std::size_t index = 0; index < grants.size(); ++index) {
            const std::string path = grant_path(index);
            if (std::optional<AuthoringError> error = check_text(grants[index].scriptTypeName, limits.maxLabelBytes, true, path))
                return error;
            if (std::optional<AuthoringError> error = check_text(grants[index].componentName, limits.maxLabelBytes, true, path))
                return error;
        }
        return std::nullopt;
    }

    // Maps a runner capture failure to the session code for this stage (D3).
    static AuthoringError capture_error(const LuaManifestError& error, CaptureStage stage) {
        AuthoringErrorCode code = AuthoringErrorCode::HostError;
        switch (error.code) {
        case LuaManifestErrorCode::LimitExceeded:
            code = AuthoringErrorCode::LimitExceeded;
            break;
        case LuaManifestErrorCode::InvalidGrant:
            code = stage == CaptureStage::Admission ? AuthoringErrorCode::InvalidInput : AuthoringErrorCode::StaleScope;
            break;
        case LuaManifestErrorCode::SchemaMismatch:
            code = stage == CaptureStage::Admission ? AuthoringErrorCode::InvalidInput : AuthoringErrorCode::HostError;
            break;
        case LuaManifestErrorCode::InvalidBehavior:
        case LuaManifestErrorCode::SnapshotUnavailable:
        case LuaManifestErrorCode::HostError:
            code = AuthoringErrorCode::HostError;
            break;
        }
        return make_error(code, error.diagnostic);
    }

    // Grant-order target identities without reading values; host exceptions
    // are HostError.
    AuthoringResult<std::vector<LuaScopeTarget>> resolve_targets(
        const std::vector<ScopeGrant>& grants,
        AuthoringErrorCode unresolved,
        std::string_view message
    ) const {
        try {
            std::optional<std::vector<LuaScopeTarget>> targets = runner.scope_targets(
                runtime.world(), std::span<const ScopeGrant>(grants));
            if (!targets)
                return make_error(unresolved, message, "scope");
            return std::move(*targets);
        } catch (const std::exception& exception) {
            return make_error(AuthoringErrorCode::HostError, exception.what());
        } catch (...) {
            return make_error(AuthoringErrorCode::HostError, "unknown scope target host error");
        }
    }

    // Admission capture for create/replace: runner capture, then D2 and size checks.
    AuthoringResult<Admission> admit(const std::vector<ScopeGrant>& grants, IntentTime now) {
        const LuaManifestResult result = runner.scope_manifest(
            runtime.world(), std::span<const ScopeGrant>(grants), now);
        if (!result.ok())
            return capture_error(result.error(), CaptureStage::Admission);

        const LuaCapabilityManifest& manifest = result.manifest();
        const auto& capabilities = manifest.capabilities();
        const auto& entries = manifest.capture().entries();
        for (std::size_t index = 0; index < entries.size(); ++index) {
            if (entries[index].type != scriptType.id)
                continue;
            for (std::size_t grant = 0; grant < grants.size(); ++grant) {
                if (grants[grant].scriptTypeName == capabilities[index].scriptTypeName
                    && grants[grant].componentName == capabilities[index].componentName)
                    return make_error(
                        AuthoringErrorCode::InvalidInput,
                        "script-control components are never exposed to an authoring scope",
                        grant_path(grant));
            }
            return make_error(
                AuthoringErrorCode::InvalidInput,
                "script-control components are never exposed to an authoring scope",
                "grants");
        }

        // The exact L0 builder accounting.
        if (manifest.logical_bytes() > limits.maxManifestBytes)
            return make_error(AuthoringErrorCode::LimitExceeded, "scope manifest exceeds the session manifest limit", "grants");

        AuthoringResult<std::vector<LuaScopeTarget>> targets = resolve_targets(
            grants, AuthoringErrorCode::HostError, "scope targets changed during admission");
        if (!targets.ok())
            return targets.error();
        return Admission{manifest, targets.value()};
    }

    // Re-resolves a stored scope (D3). Target identity is compared before any
    // value is read, so a removed or recreated target is stale even when its
    // new value would fail capture.
    AuthoringResult<LuaCapabilityManifest> recapture(const ScopeRecord& record, IntentTime now) {
        AuthoringResult<std::vector<LuaScopeTarget>> targets = resolve_targets(
            record.grants, AuthoringErrorCode::StaleScope, "a scope target was removed or recreated");
        if (!targets.ok())
            return targets.error();
        if (targets.value() != record.targets)
            return make_error(AuthoringErrorCode::StaleScope, "a scope target was removed or recreated", "scope");

        const LuaManifestResult result = runner.scope_manifest(
            runtime.world(), std::span<const ScopeGrant>(record.grants), now);
        if (!result.ok())
            return capture_error(result.error(), CaptureStage::Recapture);
        if (!same_entries(result.manifest().capture(), record.capture))
            return make_error(AuthoringErrorCode::StaleScope, "a scope target was removed or recreated", "scope");
        if (result.manifest().logical_bytes() > limits.maxManifestBytes)
            return make_error(AuthoringErrorCode::LimitExceeded, "scope manifest exceeds the session manifest limit", "scope");
        return result.manifest();
    }

    const ScopeRecord* owned_scope(const CallerContext& caller, ScopeId scope) const {
        if (caller.session != id)
            return nullptr;
        const auto found = scopes.find(scope);
        if (found == scopes.end() || found->second.owner != caller.owner)
            return nullptr;
        return &found->second;
    }
};

AuthoringSession::AuthoringSession(
    AuthoringSessionId id,
    Runtime& runtime,
    scripting::LuaBehaviorRunner& runner,
    ComponentType<scripting::LuaBehaviorScript> scriptType,
    AuthoringLimits limits
) {
    if (scriptType.id == InvalidComponentTypeId || scriptType.world != runtime.world().instance_id())
        throw std::invalid_argument("authoring session script type does not belong to this World");
    // World's typed check rejects a stale generation, an unregistered id and
    // an id registered for another component type.
    try {
        (void)runtime.world().component_type(scriptType);
    } catch (const std::exception&) {
        throw std::invalid_argument("authoring session script type is not a current LuaBehaviorScript type of this World");
    }
    if (!valid_limits(limits))
        throw std::invalid_argument("authoring session limits must be nonzero and within the contract defaults");

    impl = std::make_unique<Impl>(Impl{
        std::move(id),
        limits,
        runtime,
        runner,
        scriptType,
        std::nullopt,
        0,
        0,
        {},
        {},
        0
    });
}

AuthoringSession::~AuthoringSession() = default;

const AuthoringSessionId& AuthoringSession::id() const {
    return impl->id;
}

const AuthoringLimits& AuthoringSession::limits() const {
    return impl->limits;
}

AuthoringResult<ScopeView> AuthoringSession::create_scope(
    std::string owner,
    std::vector<ScopeGrant> grants,
    IntentTime now
) {
    if (std::optional<AuthoringError> error = impl->check_time(now))
        return std::move(*error);
    if (std::optional<AuthoringError> error = check_text(owner, impl->limits.maxLabelBytes, true, "owner"))
        return std::move(*error);
    if (std::optional<AuthoringError> error = impl->check_grants(grants))
        return std::move(*error);
    if (impl->scopes.size() >= impl->limits.maxActiveScopes)
        return make_error(AuthoringErrorCode::LimitExceeded, "session has no free active scope slot");
    const std::optional<std::uint64_t> issued = checked_next(impl->lastScope, impl->limits.maxIdValue);
    if (!issued)
        return make_error(AuthoringErrorCode::LimitExceeded, "session scope id counter is exhausted");

    AuthoringResult<Impl::Admission> admitted = impl->admit(grants, now);
    if (!admitted.ok())
        return admitted.error();

    const Impl::Admission& admission = admitted.value();
    const std::size_t charge = Impl::scope_charge(owner, grants, admission);
    if (!impl->payload_fits(0, charge))
        return make_error(AuthoringErrorCode::LimitExceeded, "scope exceeds the session payload ceiling");

    const ScopeId scope(*issued);
    impl->scopes.emplace(scope, Impl::ScopeRecord{
        std::move(owner), std::move(grants), 1, admission.manifest.capture(), admission.targets, charge});
    impl->sessionPayloadBytes += charge;
    impl->lastScope = *issued;
    impl->lastNow = now;
    return ScopeView{scope, 1, admission.manifest};
}

AuthoringResult<ScopeView> AuthoringSession::replace_scope(
    ScopeId scope,
    ScopeRevision expectedRevision,
    std::vector<ScopeGrant> grants,
    IntentTime now
) {
    const auto found = impl->scopes.find(scope);
    if (found == impl->scopes.end())
        return make_error(AuthoringErrorCode::NotFound, "scope does not exist", "scope");
    if (std::optional<AuthoringError> error = impl->check_time(now))
        return std::move(*error);
    if (found->second.revision != expectedRevision)
        return make_error(AuthoringErrorCode::StaleScope, "expected scope revision is not current", "expectedRevision");
    if (std::optional<AuthoringError> error = impl->check_grants(grants))
        return std::move(*error);
    const std::optional<std::uint64_t> revision = checked_next(found->second.revision, MaxUint64);
    if (!revision)
        return make_error(AuthoringErrorCode::LimitExceeded, "scope revision counter is exhausted", "scope");

    AuthoringResult<Impl::Admission> admitted = impl->admit(grants, now);
    if (!admitted.ok())
        return admitted.error();

    Impl::ScopeRecord& record = found->second;
    const Impl::Admission& admission = admitted.value();
    const std::size_t charge = Impl::scope_charge(record.owner, grants, admission);
    if (!impl->payload_fits(record.payloadCharge, charge))
        return make_error(AuthoringErrorCode::LimitExceeded, "replacement scope exceeds the session payload ceiling", "grants");

    // Every allocating copy happens before the commit, so a throw leaves the
    // record and the counter as they were; the commit only moves and assigns.
    LuaManifestCapture capture = admission.manifest.capture();
    std::vector<LuaScopeTarget> targets = admission.targets;
    ScopeView view{scope, *revision, admission.manifest};
    const std::size_t retained = impl->sessionPayloadBytes - record.payloadCharge + charge;
    static_assert(std::is_nothrow_move_assignable_v<std::vector<ScopeGrant>>);
    static_assert(std::is_nothrow_move_assignable_v<LuaManifestCapture>);
    static_assert(std::is_nothrow_move_assignable_v<std::vector<LuaScopeTarget>>);
    static_assert(std::is_nothrow_move_constructible_v<ScopeView>);

    record.grants = std::move(grants);
    record.revision = *revision;
    record.capture = std::move(capture);
    record.targets = std::move(targets);
    record.payloadCharge = charge;
    impl->sessionPayloadBytes = retained;
    impl->lastNow = now;
    return view;
}

AuthoringResult<void> AuthoringSession::revoke_scope(ScopeId scope, ScopeRevision expectedRevision) {
    const auto found = impl->scopes.find(scope);
    if (found == impl->scopes.end())
        return make_error(AuthoringErrorCode::NotFound, "scope does not exist", "scope");
    if (found->second.revision != expectedRevision)
        return make_error(AuthoringErrorCode::StaleScope, "expected scope revision is not current", "expectedRevision");

    // Proposals made under the scope are retained and keep their charges.
    impl->sessionPayloadBytes -= found->second.payloadCharge;
    impl->scopes.erase(found);
    return {};
}

AuthoringResult<ScopeView> AuthoringSession::discover(
    const CallerContext& caller,
    ScopeId scope,
    IntentTime now
) {
    const Impl::ScopeRecord* record = impl->owned_scope(caller, scope);
    if (!record)
        return make_error(AuthoringErrorCode::NotFound, "scope does not exist for this caller", "scope");
    if (std::optional<AuthoringError> error = impl->check_time(now))
        return std::move(*error);

    AuthoringResult<LuaCapabilityManifest> captured = impl->recapture(*record, now);
    if (!captured.ok())
        return captured.error();

    impl->lastNow = now;
    return ScopeView{scope, record->revision, captured.value()};
}

AuthoringResult<ProposalId> AuthoringSession::submit(
    const CallerContext& caller,
    ProposalSubmission submission
) {
    const Impl::ScopeRecord* record = impl->owned_scope(caller, submission.scope);
    if (!record)
        return make_error(AuthoringErrorCode::NotFound, "scope does not exist for this caller", "scope");
    if (record->revision != submission.expectedScopeRevision)
        return make_error(AuthoringErrorCode::StaleScope, "expected scope revision is not current", "expectedScopeRevision");

    const std::size_t sourceLimit = std::min(impl->limits.maxSourceBytes, record->capture.limits().maxSourceBytes);
    if (std::optional<AuthoringError> error = check_text(submission.source, sourceLimit, true, "source"))
        return std::move(*error);
    if (std::optional<AuthoringError> error = check_text(submission.rationale, impl->limits.maxRationaleBytes, false, "rationale"))
        return std::move(*error);

    if (submission.managedBehavior.has_value() != submission.expectedManagedRevision.has_value())
        return make_error(
            AuthoringErrorCode::InvalidInput,
            "managed behavior and expected managed revision must be supplied together",
            submission.managedBehavior ? "expectedManagedRevision" : "managedBehavior");
    if (submission.managedBehavior)
        return make_error(AuthoringErrorCode::NotFound, "managed behavior does not exist in this scope", "managedBehavior");

    if (impl->proposals.size() >= impl->limits.maxProposals)
        return make_error(AuthoringErrorCode::LimitExceeded, "session proposal capacity is exhausted");
    const std::optional<std::uint64_t> issued = checked_next(impl->lastProposal, impl->limits.maxIdValue);
    if (!issued)
        return make_error(AuthoringErrorCode::LimitExceeded, "session proposal id counter is exhausted");

    const ProposalId proposal(*issued);
    Impl::ProposalRecord candidate{
        BehaviorProposal{
            proposal,
            impl->id,
            submission.scope,
            record->revision,
            std::string(scripting::LuaAuthoringContract),
            std::move(submission.source),
            std::move(submission.rationale),
            std::nullopt
        },
        record->owner,
        record->capture
    };
    const std::size_t charge = Impl::proposal_charge(candidate);
    if (!impl->payload_fits(0, charge))
        return make_error(AuthoringErrorCode::LimitExceeded, "proposal exceeds the session payload ceiling");

    // Liveness only; the admitted record keeps the scope's target identities.
    AuthoringResult<LuaCapabilityManifest> captured = impl->recapture(*record, impl->lastNow.value_or(0));
    if (!captured.ok())
        return captured.error();

    impl->proposals.emplace(proposal, std::move(candidate));
    impl->sessionPayloadBytes += charge;
    impl->lastProposal = *issued;
    return proposal;
}

AuthoringResult<BehaviorProposal> AuthoringSession::proposal(
    const CallerContext& caller,
    ProposalId proposal
) const {
    if (caller.session != impl->id)
        return make_error(AuthoringErrorCode::NotFound, "proposal does not exist for this caller", "proposal");
    const auto found = impl->proposals.find(proposal);
    if (found == impl->proposals.end() || found->second.owner != caller.owner)
        return make_error(AuthoringErrorCode::NotFound, "proposal does not exist for this caller", "proposal");
    return found->second.proposal;
}

}
