#pragma once

#include "liquid/Value.hpp"

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace liquid {

struct SessionId {
    std::uint64_t value = 0;

    explicit constexpr SessionId(std::uint64_t id = 0) : value(id) {}

    constexpr bool valid() const {
        return value != 0;
    }

    auto operator<=>(const SessionId&) const = default;
};

struct CommandId {
    std::uint64_t value = 0;

    explicit constexpr CommandId(std::uint64_t id = 0) : value(id) {}

    constexpr bool valid() const {
        return value != 0;
    }

    auto operator<=>(const CommandId&) const = default;
};

struct RecordId {
    std::uint64_t value = 0;

    explicit constexpr RecordId(std::uint64_t id = 0) : value(id) {}

    constexpr bool valid() const {
        return value != 0;
    }

    auto operator<=>(const RecordId&) const = default;
};

struct EffectLimits {
    static constexpr std::size_t maxAdapterRouteBytes = 256;
    static constexpr std::size_t maxTargetBytes = 1024;
    static constexpr std::size_t maxDiagnosticBytes = 4096;
    static constexpr std::size_t maxRetryDelays = 32;
};

class AdapterRoute {
    std::string routeValue;

public:
    explicit AdapterRoute(std::string value);

    const std::string& value() const {
        return routeValue;
    }

    bool operator==(const AdapterRoute&) const = default;
};

class EffectTarget {
    std::string targetValue;

public:
    explicit EffectTarget(std::string value);

    const std::string& value() const {
        return targetValue;
    }

    bool operator==(const EffectTarget&) const = default;
};

enum class CommandStatus {
    Pending,
    Applied,
    Rejected,
    Failed,
    TimedOut,
    Superseded,
    Indeterminate
};

enum class FeedbackTiming {
    Deferred,
    Immediate
};

struct ResolvedEffect {
    AdapterRoute adapterRoute;
    EffectTarget target;
    Value desiredValue;

    bool operator==(const ResolvedEffect&) const = default;
};

struct EffectCommand {
    SessionId sessionId;
    CommandId commandId;
    ResolvedEffect effect;
    std::uint64_t issuedAtMs = 0;

    bool operator==(const EffectCommand&) const = default;
};

struct EffectReport {
    SessionId sessionId;
    CommandId commandId;
    AdapterRoute adapterRoute;
    EffectTarget target;
    CommandStatus status = CommandStatus::Failed;
    std::optional<Value> observedValue;
    std::string diagnostic;
    std::uint64_t reportedAtMs = 0;

    bool operator==(const EffectReport&) const = default;
};

struct RetryPolicy {
    std::vector<std::chrono::milliseconds> retryDelays{
        std::chrono::milliseconds{250},
        std::chrono::milliseconds{500},
        std::chrono::seconds{1},
        std::chrono::seconds{2},
        std::chrono::seconds{4}
    };
    std::chrono::milliseconds overallTimeout{std::chrono::seconds{30}};

    void validate() const;
    std::optional<std::chrono::milliseconds> delay_before_retry(
        std::size_t retryIndex
    ) const;

    bool operator==(const RetryPolicy&) const = default;
};

struct AdapterCapabilities {
    bool nativeIdempotency = false;
    bool readAfterWriteReconciliation = false;
    bool synchronousImmediateFeedback = false;

    bool operator==(const AdapterCapabilities&) const = default;
};

enum class DispatchDisposition {
    Accepted,
    Rejected,
    Failed
};

struct DispatchResult {
    DispatchDisposition disposition = DispatchDisposition::Failed;
    std::optional<EffectReport> immediateReport;
    std::string diagnostic;

    static DispatchResult accepted(
        std::optional<EffectReport> immediateReport = std::nullopt
    );
    static DispatchResult rejected(std::string diagnostic = {});
    static DispatchResult failed(std::string diagnostic = {});

    void validate() const;

    bool operator==(const DispatchResult&) const = default;
};

void validate_effect_command(const EffectCommand& command);
void validate_effect_report(const EffectReport& report);
void validate_dispatch_result_for_command(
    const DispatchResult& result,
    const EffectCommand& command
);

}
