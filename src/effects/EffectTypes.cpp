#include "liquid/effects/EffectTypes.hpp"

#include <stdexcept>

namespace liquid {

namespace {

void validate_bounded_text(
    const std::string& value,
    std::size_t maximumBytes,
    const char* field
) {
    if (value.empty())
        throw std::invalid_argument(std::string(field) + " must not be empty");
    if (value.size() > maximumBytes)
        throw std::invalid_argument(std::string(field) + " exceeds its size limit");
    if (value.find('\0') != std::string::npos)
        throw std::invalid_argument(std::string(field) + " must not contain NUL bytes");
}

void validate_diagnostic(const std::string& diagnostic) {
    if (diagnostic.size() > EffectLimits::maxDiagnosticBytes)
        throw std::invalid_argument("effect diagnostic exceeds its size limit");
    Value(diagnostic).validate();
}

}

AdapterRoute::AdapterRoute(std::string value) : routeValue(std::move(value)) {
    validate_bounded_text(
        routeValue,
        EffectLimits::maxAdapterRouteBytes,
        "adapter route"
    );

    for (const unsigned char character : routeValue) {
        const bool valid =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '.' || character == '_' || character == '-' || character == '/';
        if (!valid)
            throw std::invalid_argument("adapter route contains an invalid character");
    }
}

EffectTarget::EffectTarget(std::string value) : targetValue(std::move(value)) {
    validate_bounded_text(targetValue, EffectLimits::maxTargetBytes, "effect target");
    Value(targetValue).validate();
}

void RetryPolicy::validate() const {
    if (retryDelays.size() > EffectLimits::maxRetryDelays)
        throw std::invalid_argument("retry policy has too many retry delays");
    if (overallTimeout <= std::chrono::milliseconds::zero())
        throw std::invalid_argument("retry timeout must be positive");

    auto previous = std::chrono::milliseconds::zero();
    for (const auto delay : retryDelays) {
        if (delay <= previous)
            throw std::invalid_argument("retry delays must be positive and increasing");
        if (delay >= overallTimeout)
            throw std::invalid_argument("retry delay must precede the overall timeout");
        previous = delay;
    }
}

std::optional<std::chrono::milliseconds> RetryPolicy::delay_before_retry(
    std::size_t retryIndex
) const {
    validate();
    if (retryIndex >= retryDelays.size())
        return std::nullopt;
    return retryDelays[retryIndex];
}

DispatchResult DispatchResult::accepted(
    std::optional<EffectReport> immediateReportValue
) {
    DispatchResult result{
        DispatchDisposition::Accepted,
        std::move(immediateReportValue),
        {}
    };
    result.validate();
    return result;
}

DispatchResult DispatchResult::rejected(std::string diagnosticValue) {
    DispatchResult result{
        DispatchDisposition::Rejected,
        std::nullopt,
        std::move(diagnosticValue)
    };
    result.validate();
    return result;
}

DispatchResult DispatchResult::failed(std::string diagnosticValue) {
    DispatchResult result{
        DispatchDisposition::Failed,
        std::nullopt,
        std::move(diagnosticValue)
    };
    result.validate();
    return result;
}

void DispatchResult::validate() const {
    validate_diagnostic(diagnostic);
    if (disposition != DispatchDisposition::Accepted && immediateReport)
        throw std::invalid_argument(
            "only an accepted dispatch may include an immediate report"
        );
    if (immediateReport)
        validate_effect_report(*immediateReport);
}

void validate_effect_command(const EffectCommand& command) {
    if (!command.sessionId.valid())
        throw std::invalid_argument("effect command requires a valid session ID");
    if (!command.commandId.valid())
        throw std::invalid_argument("effect command requires a valid command ID");
    command.effect.desiredValue.validate();
}

void validate_effect_report(const EffectReport& report) {
    if (!report.sessionId.valid())
        throw std::invalid_argument("effect report requires a valid session ID");
    if (!report.commandId.valid())
        throw std::invalid_argument("effect report requires a valid command ID");
    if (report.status == CommandStatus::Pending)
        throw std::invalid_argument("pending is not a report outcome");
    if (report.status == CommandStatus::Applied && !report.observedValue)
        throw std::invalid_argument("an applied report requires an observed value");
    if (report.status != CommandStatus::Applied && report.observedValue)
        throw std::invalid_argument("only an applied report may carry an observed value");
    if (report.observedValue)
        report.observedValue->validate();
    validate_diagnostic(report.diagnostic);
}

void validate_dispatch_result_for_command(
    const DispatchResult& result,
    const EffectCommand& command
) {
    validate_effect_command(command);
    result.validate();
    if (!result.immediateReport)
        return;

    const auto& report = *result.immediateReport;
    if (report.sessionId != command.sessionId ||
        report.commandId != command.commandId ||
        report.adapterRoute != command.effect.adapterRoute ||
        report.target != command.effect.target) {
        throw std::invalid_argument(
            "immediate report does not match the dispatched command"
        );
    }
}

}
