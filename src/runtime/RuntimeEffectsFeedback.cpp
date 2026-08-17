#include "RuntimeInternals.hpp"

#include <string>
#include <utility>
#include <vector>

namespace liquid::detail {

void RuntimeEffectsState::apply_report(const EffectReport& report, std::vector<EffectReport>& accepted) {
    validate_effect_report(report);

    const auto found = commands.find(report.commandId.value);
    if (report.sessionId != session || found == commands.end()) {
        record_report(report, "rejected-unknown-or-cross-session");
        return;
    }

    CommandState& state = found->second;
    if (report.adapterRoute != state.command.effect.adapterRoute ||
        report.target != state.command.effect.target) {
        record_report(report, "rejected-mismatched-target");
        return;
    }

    if (state.terminalReport) {
        if (*state.terminalReport == report) {
            record_report(report, "duplicate");
            return;
        }
        record_report(report, "rejected-conflicting-duplicate");
        return;
    }

    if (state.status == CommandStatus::Superseded &&
        report.status != CommandStatus::Applied) {
        record_report(report, "rejected-stale-terminal");
        return;
    }

    record_report(report, "accepted");
    const bool wasSuperseded = state.status == CommandStatus::Superseded;
    if (!wasSuperseded)
        transition(state, report.status, report.diagnostic);
    state.terminalReport = report;
    accepted.push_back(report);

    if (report.status != CommandStatus::Applied)
        return;

    const TargetKey key = target_key(report.adapterRoute, report.target);
    const auto revision = observedRevisions.find(key);
    if (revision != observedRevisions.end() &&
        revision->second > report.stateRevision) {
        return;
    }
    if (revision != observedRevisions.end() &&
        revision->second == report.stateRevision) {
        const auto prior = observed.find(key);
        if (prior != observed.end() &&
            prior->second == *report.observedValue) {
            return;
        }
        record_report(report, "rejected-conflicting-state-revision");
        return;
    }

    commit_authoritative(
        report.adapterRoute,
        report.target,
        *report.observedValue,
        report.stateRevision,
        report.commandId.value);
}

// The one place an authoritative command outcome becomes observed state:
// the ObservedStateChanged evidence and the observed/revision/authority
// maps must always change together, from validated reports here and from
// host reconciliation in Runtime::reconcile_indeterminate.
void RuntimeEffectsState::commit_authoritative(
    const AdapterRoute& route,
    const EffectTarget& target,
    const Value& observedValue,
    StateRevision revision,
    std::uint64_t commandId
) {
    append(EventType::ObservedStateChanged, event_payload({
        {"key", Value{route.value() + ":" + target.value()}},
        {"value", observedValue},
        {"command_id", Value{commandId}},
        {"route", Value{route.value()}},
        {"target", Value{target.value()}},
        {"state_revision", Value{revision.value}},
        {"observed", observedValue}
    }));
    const TargetKey key = target_key(route, target);
    observed.insert_or_assign(key, observedValue);
    observedRevisions.insert_or_assign(key, revision);
    authoritativeByTarget.insert_or_assign(key, commandId);
}

void RuntimeEffectsState::apply_observation(
    const ExternalObservation& observation,
    std::vector<ExternalObservation>& accepted
) {
    validate_external_observation(observation);
    const TargetKey key = target_key(
        observation.adapterRoute, observation.target);
    std::string disposition = "accepted";
    if (observation.sessionId != session ||
        adapters.find(observation.adapterRoute.value()) == adapters.end()) {
        disposition = "rejected-unknown-or-cross-session";
    } else {
        const auto revision = observedRevisions.find(key);
        if (revision != observedRevisions.end() &&
            revision->second > observation.stateRevision) {
            disposition = "stale";
        } else if (revision != observedRevisions.end() &&
            revision->second == observation.stateRevision) {
            const auto prior = observed.find(key);
            disposition = prior != observed.end() &&
                prior->second == observation.observedValue
                ? "duplicate" : "rejected-conflicting-revision";
        }
    }

    append(EventType::ExternalObservationReceived, event_payload({
        {"session", Value{observation.sessionId.value}},
        {"route", Value{observation.adapterRoute.value()}},
        {"target", Value{observation.target.value()}},
        {"state_revision", Value{observation.stateRevision.value}},
        {"observed_at", Value{observation.observedAtMs}},
        {"observed", observation.observedValue},
        {"disposition", Value{disposition}}
    }));
    if (disposition != "accepted")
        return;

    append(EventType::ObservedStateChanged, event_payload({
        {"key", Value{observation.adapterRoute.value() + ":" +
            observation.target.value()}},
        {"value", observation.observedValue},
        {"route", Value{observation.adapterRoute.value()}},
        {"target", Value{observation.target.value()}},
        {"state_revision", Value{observation.stateRevision.value}},
        {"observed", observation.observedValue},
        {"source", Value{"external"}}
    }));
    observed.insert_or_assign(key, observation.observedValue);
    observedRevisions.insert_or_assign(key, observation.stateRevision);
    accepted.push_back(observation);
}

void RuntimeEffectsState::process_feedback(
    std::vector<EffectReport>& accepted,
    std::vector<ExternalObservation>& observations
) {
    while (!deferredSynchronousReports.empty()) {
        apply_report(deferredSynchronousReports.front(), accepted);
        deferredSynchronousReports.pop_front();
    }
    while (auto report = feedback.receiver.try_receive()) {
        try {
            apply_report(*report, accepted);
        } catch (...) {
            deferredSynchronousReports.push_front(std::move(*report));
            throw;
        }
    }
    while (auto observation = feedback.receiver.try_receive_observation())
        apply_observation(*observation, observations);
}

bool RuntimeEffectsState::report_is_authoritative(const EffectReport& report) const {
    if (report.status != CommandStatus::Applied)
        return false;
    const TargetKey key = target_key(
        report.adapterRoute, report.target);
    const auto authority = authoritativeByTarget.find(key);
    const auto revision = observedRevisions.find(key);
    const auto value = observed.find(key);
    return authority != authoritativeByTarget.end() &&
        authority->second == report.commandId.value &&
        revision != observedRevisions.end() &&
        revision->second == report.stateRevision &&
        value != observed.end() && value->second == *report.observedValue;
}

bool RuntimeEffectsState::observation_is_authoritative(
    const ExternalObservation& observation
) const {
    const TargetKey key = target_key(
        observation.adapterRoute, observation.target);
    const auto revision = observedRevisions.find(key);
    const auto value = observed.find(key);
    return revision != observedRevisions.end() &&
        revision->second == observation.stateRevision &&
        value != observed.end() &&
        value->second == observation.observedValue;
}

}
