#pragma once

#include "liquid/scripting/LuaCapabilityManifest.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace liquid::authoring {

inline constexpr std::size_t AuthoringMaxDiagnosticBytes = 4'096;
inline constexpr std::size_t AuthoringSessionIdHexDigits = 32;

// The trusted-host alias for a prospective grant; not a second representation.
using ScopeGrant = scripting::LuaScopeGrant;
using ScopeRevision = std::uint64_t;
using ManagedBehaviorRevision = std::uint64_t;

enum class AuthoringErrorCode {
    InvalidInput,
    NotFound,
    AccessDenied,
    StaleScope,
    StaleApproval,
    Unsupported,
    LimitExceeded,
    EvaluationFailed,
    HostError,
    NeedsIntervention
};

// `diagnostic` plus `fieldPath` fit in AuthoringMaxDiagnosticBytes; a
// truncated diagnostic ends in "...". Machine meaning lives in `code` only.
struct AuthoringError {
    AuthoringErrorCode code = AuthoringErrorCode::HostError;
    std::string diagnostic;
    std::optional<std::string> fieldPath;
};

// Exactly one value or one error.
template <typename T>
class AuthoringResult {
public:
    AuthoringResult(T value) : outcome(std::in_place_index<0>, std::move(value)) {}
    AuthoringResult(AuthoringError error) : outcome(std::in_place_index<1>, std::move(error)) {}

    bool ok() const {
        return outcome.index() == 0;
    }

    // Throws std::logic_error when the call failed.
    const T& value() const {
        if (!ok())
            throw std::logic_error("authoring result holds an error");
        return std::get<0>(outcome);
    }

    // Throws std::logic_error when the call succeeded.
    const AuthoringError& error() const {
        if (ok())
            throw std::logic_error("authoring result holds a value");
        return std::get<1>(outcome);
    }

private:
    std::variant<T, AuthoringError> outcome;
};

// Empty success, distinct from failure.
template <>
class AuthoringResult<void> {
public:
    AuthoringResult() = default;
    AuthoringResult(AuthoringError error) : failure(std::move(error)) {}

    bool ok() const {
        return !failure.has_value();
    }

    // Throws std::logic_error when the call succeeded.
    const AuthoringError& error() const {
        if (ok())
            throw std::logic_error("authoring result holds no error");
        return *failure;
    }

private:
    std::optional<AuthoringError> failure;
};

// Trusted-host identity of one session: 32 lowercase hexadecimal characters,
// unique across the host's sessions and never reused after restart.
class AuthoringSessionId {
public:
    static AuthoringResult<AuthoringSessionId> from_hex(std::string_view text);

    const std::string& hex() const;

    friend bool operator==(const AuthoringSessionId&, const AuthoringSessionId&) = default;
    friend std::strong_ordering operator<=>(const AuthoringSessionId&, const AuthoringSessionId&) = default;

private:
    std::string digits;

    explicit AuthoringSessionId(std::string hex);
};

// Nonzero per-session monotonic locator; never recycled. Not a credential:
// every lookup still checks the caller.
template <typename Tag>
class StrongId {
public:
    constexpr StrongId() = default;
    constexpr explicit StrongId(std::uint64_t raw) : rawValue(raw) {}

    constexpr std::uint64_t value() const {
        return rawValue;
    }

    constexpr bool valid() const {
        return rawValue != 0;
    }

    friend constexpr bool operator==(StrongId, StrongId) = default;
    friend constexpr std::strong_ordering operator<=>(StrongId, StrongId) = default;

private:
    std::uint64_t rawValue = 0;
};

using ScopeId = StrongId<struct ScopeIdTag>;
using ProposalId = StrongId<struct ProposalIdTag>;
// Reserved for L5; no L1 call creates managed behaviors.
using ManagedBehaviorId = StrongId<struct ManagedBehaviorIdTag>;

// Bound by the host/transport, never parsed from a caller-supplied argument.
struct CallerContext {
    AuthoringSessionId session;
    std::string owner;
};

// Contract defaults. A host may lower any limit, never raise one; zero is invalid.
struct AuthoringLimits {
    std::size_t maxActiveScopes = 64;
    std::size_t maxProposals = 128;
    std::size_t maxEvaluations = 256;
    std::size_t maxManagedBehaviors = 64;
    std::size_t maxSourceBytes = 65'536;
    std::size_t maxRationaleBytes = 2'048;
    std::size_t maxScopeCapabilities = 128;
    std::size_t maxLabelBytes = 256;
    std::size_t maxManifestBytes = 1024 * 1024;
    // Highest value any per-session ID counter may issue; exhaustion is LimitExceeded.
    std::uint64_t maxIdValue = std::numeric_limits<std::uint64_t>::max();
};

}
