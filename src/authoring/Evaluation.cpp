#include "BoundedEvaluationStore.hpp"

#include "liquid/scripting/LuaCapabilityManifest.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace liquid::authoring::detail {

namespace {

using scripting::LuaCapabilityManifest;
using scripting::LuaExecutionLimits;
using scripting::LuaExecutionResult;
using scripting::LuaExecutionStatus;
using scripting::LuaManifestCapability;
using scripting::LuaSchemaField;
using scripting::LuaSchemaKind;
using scripting::LuaValue;
using scripting::LuaValueSchema;

// Isolated evidence never carries a live session identity.
constexpr std::uint64_t EvaluationSessionId = 1;
constexpr std::string_view Ellipsis = "...";
constexpr std::string_view FixtureMismatch = "fixture mismatch: ";
constexpr std::string_view BudgetExhausted = "evaluation event-store budget exhausted";

constexpr std::size_t elements_bytes(std::size_t count, std::size_t each) {
    return count > std::numeric_limits<std::size_t>::max() / each
        ? std::numeric_limits<std::size_t>::max()
        : count * each;
}

// shortcut: second copy of the diagnostic truncation in AuthoringSession.cpp
// (make_error also budgets a field path). Upgrade trigger: a third copy.
std::string bounded_diagnostic(std::string_view message) {
    if (message.empty())
        message = "evaluation case failed";
    if (message.size() <= AuthoringMaxDiagnosticBytes)
        return std::string(message);
    std::size_t cut = AuthoringMaxDiagnosticBytes - Ellipsis.size();
    // Do not split a UTF-8 sequence.
    while (cut > 0 && (static_cast<unsigned char>(message[cut]) & 0xC0) == 0x80)
        --cut;
    return std::string(message.substr(0, cut)) + std::string(Ellipsis);
}

std::size_t scalar_bytes(const Value& value) {
    switch (value.kind()) {
    case Value::Kind::Boolean:
        return 1;
    case Value::Kind::SignedInteger:
    case Value::Kind::UnsignedInteger:
    case Value::Kind::Double:
        return LogicalScalarBytes;
    case Value::Kind::String:
        return value.as_string().size();
    case Value::Kind::Bytes:
        return value.as_bytes().size();
    case Value::Kind::Null:
    case Value::Kind::Array:
    case Value::Kind::Object:
        break;
    }
    return 0;
}

// ---- Semantic manifest comparison (handles and readValue are ignored) ----

bool same_schema(const LuaValueSchema& left, const LuaValueSchema& right);

auto schema_bounds(const LuaValueSchema& schema) {
    return std::make_tuple(
        schema.kind(), schema.description(),
        schema.integer_minimum(), schema.integer_maximum(),
        schema.number_minimum(), schema.number_maximum(),
        schema.minimum_bytes(), schema.maximum_bytes(), schema.enumeration(),
        schema.minimum_items(), schema.maximum_items());
}

bool same_fields(const std::vector<LuaSchemaField>& left, const std::vector<LuaSchemaField>& right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].name != right[index].name
            || left[index].required != right[index].required
            || !same_schema(left[index].schema, right[index].schema))
            return false;
    }
    return true;
}

bool same_schema(const LuaValueSchema& left, const LuaValueSchema& right) {
    if (schema_bounds(left) != schema_bounds(right))
        return false;
    if (left.kind() == LuaSchemaKind::Array && !same_schema(left.item(), right.item()))
        return false;
    return same_fields(left.fields(), right.fields());
}

bool same_optional_schema(const std::optional<LuaValueSchema>& left, const std::optional<LuaValueSchema>& right) {
    if (left.has_value() != right.has_value())
        return false;
    return !left || same_schema(*left, *right);
}

auto limit_fields(const LuaExecutionLimits& limits) {
    return std::tie(
        limits.maxSourceBytes, limits.maxMemoryBytes, limits.maxInstructions,
        limits.maxDiagnosticBytes, limits.maxCreatedIntents, limits.maxCancelledIntents,
        limits.maxWatches, limits.maxTableDepth, limits.maxTableEntries,
        limits.maxStringBytes, limits.maxBufferedValueBytes, limits.recordFullSource);
}

std::vector<const LuaManifestCapability*> sorted_capabilities(const LuaCapabilityManifest& manifest) {
    std::vector<const LuaManifestCapability*> sorted;
    for (const LuaManifestCapability& capability : manifest.capabilities())
        sorted.push_back(&capability);
    std::sort(sorted.begin(), sorted.end(), [](const auto* left, const auto* right) {
        return std::tie(left->scriptTypeName, left->componentName)
            < std::tie(right->scriptTypeName, right->componentName);
    });
    return sorted;
}

std::optional<std::string> capability_mismatch(
    const LuaManifestCapability& scope,
    const LuaManifestCapability& isolated
) {
    const std::string target = "capability " + scope.scriptTypeName + "." + scope.componentName;
    if (isolated.scriptTypeName != scope.scriptTypeName || isolated.componentName != scope.componentName)
        return target + " is not granted in the isolated world";
    if (isolated.mode != scope.mode)
        return target + " has a different access mode";
    if (isolated.accessExpression != scope.accessExpression || isolated.description != scope.description)
        return target + " has different binding metadata";
    if (!same_optional_schema(isolated.readSchema, scope.readSchema)
        || !same_optional_schema(isolated.writeSchema, scope.writeSchema))
        return target + " has a different component schema";
    return std::nullopt;
}

std::optional<std::string> manifest_mismatch(
    const LuaCapabilityManifest& scope,
    const LuaCapabilityManifest& isolated
) {
    if (limit_fields(scope.limits()) != limit_fields(isolated.limits()))
        return std::string("isolated authoring limits differ from the captured scope");
    const std::vector<const LuaManifestCapability*> expected = sorted_capabilities(scope);
    const std::vector<const LuaManifestCapability*> actual = sorted_capabilities(isolated);
    if (expected.size() != actual.size())
        return "isolated world grants " + std::to_string(actual.size())
            + " capabilities; the captured scope has " + std::to_string(expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (std::optional<std::string> mismatch = capability_mismatch(*expected[index], *actual[index]))
            return mismatch;
    }
    return std::nullopt;
}

// ---- Value conversion and trace normalization ----

Value to_value(const LuaValue& value) {
    return std::visit([](const auto& stored) -> Value {
        using Stored = std::decay_t<decltype(stored)>;
        if constexpr (std::is_same_v<Stored, LuaValue::Array>) {
            Value::Array items;
            items.reserve(stored.size());
            for (const LuaValue& item : stored)
                items.push_back(to_value(item));
            return Value(std::move(items));
        } else if constexpr (std::is_same_v<Stored, LuaValue::Table>) {
            Value::Object fields;
            for (const auto& [key, item] : stored)
                fields.emplace(key, to_value(item));
            return Value(std::move(fields));
        } else {
            return Value(stored);
        }
    }, value.storage());
}

bool handle_key(std::string_view key) {
    return key == "slot" || key == "generation" || key == "session" || key == "world"
        || key.ends_with("_slot") || key.ends_with("_generation") || key.ends_with("_world");
}

// Per key family: raw handle number -> first-seen ordinal (from 1).
using Ordinals = std::map<std::string, std::map<std::uint64_t, std::uint64_t>, std::less<>>;

Value normalize_value(const Value& value, std::string_view key, Ordinals& ordinals) {
    if (value.kind() == Value::Kind::UnsignedInteger && handle_key(key)) {
        auto& family = ordinals[std::string(key)];
        const auto next = static_cast<std::uint64_t>(family.size()) + 1;
        return Value(family.emplace(value.as_unsigned_integer(), next).first->second);
    }
    if (value.kind() == Value::Kind::Array) {
        Value::Array items;
        for (const Value& item : value.as_array())
            items.push_back(normalize_value(item, key, ordinals));
        return Value(std::move(items), Value::Unchecked{});
    }
    if (value.kind() == Value::Kind::Object) {
        Value::Object fields;
        for (const auto& [field, item] : value.as_object())
            fields.emplace(field, normalize_value(item, field, ordinals));
        return Value(std::move(fields), Value::Unchecked{});
    }
    return value;
}

void append_trace(std::size_t caseIndex, const std::vector<EventRecord>& records, std::vector<EvaluationTraceEntry>& trace) {
    // Ordinals restart per case: every case has its own fresh World.
    Ordinals ordinals;
    for (const EventRecord& record : records)
        trace.push_back(EvaluationTraceEntry{
            caseIndex, record.type, record.version, normalize_value(record.payload, {}, ordinals)});
}

// ---- Status mapping ----

// Indexed by LuaExecutionStatus declaration order.
constexpr EvaluationCaseStatus LifecycleCaseStatus[] = {
    EvaluationCaseStatus::Passed,        // Success
    EvaluationCaseStatus::Failed,        // InvalidBehavior
    EvaluationCaseStatus::LimitExceeded, // SourceLimitExceeded
    EvaluationCaseStatus::Failed,        // SyntaxError
    EvaluationCaseStatus::Failed,        // RuntimeError
    EvaluationCaseStatus::LimitExceeded, // InstructionLimitExceeded
    EvaluationCaseStatus::LimitExceeded, // MemoryLimitExceeded
    EvaluationCaseStatus::LimitExceeded, // IntentLimitExceeded
    EvaluationCaseStatus::Failed,        // InvalidProposal
    EvaluationCaseStatus::Failed,        // CommitFailed
    EvaluationCaseStatus::HostError,     // HostError
};
static_assert(static_cast<std::size_t>(LuaExecutionStatus::InvalidBehavior) == 1
    && static_cast<std::size_t>(LuaExecutionStatus::SourceLimitExceeded) == 2
    && static_cast<std::size_t>(LuaExecutionStatus::SyntaxError) == 3
    && static_cast<std::size_t>(LuaExecutionStatus::RuntimeError) == 4
    && static_cast<std::size_t>(LuaExecutionStatus::InstructionLimitExceeded) == 5
    && static_cast<std::size_t>(LuaExecutionStatus::MemoryLimitExceeded) == 6
    && static_cast<std::size_t>(LuaExecutionStatus::IntentLimitExceeded) == 7
    && static_cast<std::size_t>(LuaExecutionStatus::InvalidProposal) == 8
    && static_cast<std::size_t>(LuaExecutionStatus::CommitFailed) == 9
    && static_cast<std::size_t>(LuaExecutionStatus::HostError) == std::size(LifecycleCaseStatus) - 1);

EvaluationCaseStatus lifecycle_case_status(LuaExecutionStatus status) {
    const auto index = static_cast<std::size_t>(status);
    return index < std::size(LifecycleCaseStatus) ? LifecycleCaseStatus[index] : EvaluationCaseStatus::HostError;
}

EvaluationStatus record_status(EvaluationCaseStatus status) {
    switch (status) {
    case EvaluationCaseStatus::Passed:
        return EvaluationStatus::Passed;
    case EvaluationCaseStatus::Failed:
        return EvaluationStatus::Failed;
    case EvaluationCaseStatus::LimitExceeded:
        return EvaluationStatus::LimitExceeded;
    case EvaluationCaseStatus::HostError:
    case EvaluationCaseStatus::NotRun:
        break;
    }
    return EvaluationStatus::HostError;
}

bool selected_any(const FrameLog& frame, const std::vector<IntentId>& candidateIntents) {
    for (const auto& [type, components] : frame.intent_selections) {
        for (const auto& [name, intent] : components) {
            if (std::find(candidateIntents.begin(), candidateIntents.end(), intent) != candidateIntents.end())
                return true;
        }
    }
    return false;
}

// ---- One case: fresh store, fresh world, explicit frames ----

class CaseExecution {
public:
    CaseExecution(const EvaluationRun& evaluation, const EvaluationCase& evaluationCase, EvaluationRunBudget& budget)
        : run(evaluation),
          testCase(evaluationCase),
          store(EventStoreMetadata{SessionId(EvaluationSessionId), "0.1.0", evaluationCase.feedbackTiming}, budget)
    {
        result.name = testCase.name;
        result.status = EvaluationCaseStatus::Passed;
        result.framesScheduled = testCase.frameTimes.size();
    }

    EvaluationCaseResult execute() {
        if (guarded("case factory", [this] { return prepare(); })
            && guarded("isolated manifest", [this] { return check_manifest(); })
            && guarded("granted effects", [this] { return check_granted_effects(); })) {
            (void)run_frames();
            if (prepared.runtime && prepared.runner)
                capture_final_state();
        }
        return std::move(result);
    }

    std::vector<EventRecord> records() const {
        return store.read_all();
    }

private:
    // Records the first stop only; exhaustion always reports LimitExceeded.
    bool stop(EvaluationCaseStatus status, std::string_view diagnostic) {
        if (result.status != EvaluationCaseStatus::Passed)
            return false;
        if (store.exhausted()) {
            status = EvaluationCaseStatus::LimitExceeded;
            diagnostic = BudgetExhausted;
        }
        result.status = status;
        result.diagnostic = bounded_diagnostic(diagnostic);
        return false;
    }

    // Host callbacks and Solid calls: any exception is HostError, never a throw.
    template <typename Step>
    bool guarded(std::string_view what, Step&& step) {
        try {
            return step();
        } catch (const std::exception& exception) {
            return stop(EvaluationCaseStatus::HostError, std::string(what) + " failed: " + exception.what());
        } catch (...) {
            return stop(EvaluationCaseStatus::HostError, std::string(what) + " failed with an unknown exception");
        }
    }

    bool prepare() {
        RuntimeOptions options;
        options.sessionId = SessionId(EvaluationSessionId);
        options.feedbackTiming = testCase.feedbackTiming;
        options.eventStore = &store;
        const EvaluationPreparation preparation{run.source, run.grants, testCase.inputs, testCase.adapters, options};
        prepared = testCase.prepare(preparation);
        if (!prepared.runtime || !prepared.runner)
            return stop(EvaluationCaseStatus::HostError, "case factory returned no runtime or runner");
        recordsSeen = store.read_all().size();
        return true;
    }

    // The evaluator captures the isolated manifest itself (A3).
    bool check_manifest() {
        const scripting::LuaManifestResult isolated = prepared.runner->capability_manifest(
            prepared.runtime->world(), prepared.candidate, testCase.frameTimes.front());
        if (!isolated.ok())
            return stop(EvaluationCaseStatus::HostError, std::string(FixtureMismatch) + isolated.error().diagnostic);
        if (std::optional<std::string> mismatch = manifest_mismatch(run.scope, isolated.manifest()))
            return stop(EvaluationCaseStatus::HostError, std::string(FixtureMismatch) + *mismatch);
        return true;
    }

    // Each declared address must name a captured grant, at most once, and its
    // own adapter target. Diagnostics never name the route, the target or a
    // component outside the candidate's scope.
    bool check_granted_effects() {
        std::set<std::pair<std::string, std::string>> declaredComponents;
        for (const EvaluationEffectAddress& address : prepared.grantedEffects) {
            const auto grant = std::find_if(run.grants.begin(), run.grants.end(), [&](const ScopeGrant& candidate) {
                return candidate.scriptTypeName == address.scriptTypeName
                    && candidate.componentName == address.componentName;
            });
            if (grant == run.grants.end())
                return stop(EvaluationCaseStatus::HostError,
                    std::string(FixtureMismatch) + "effect address for an ungranted component");
            if (!declaredComponents.emplace(address.scriptTypeName, address.componentName).second)
                return stop(EvaluationCaseStatus::HostError, std::string(FixtureMismatch) + "effect address for "
                    + address.scriptTypeName + "." + address.componentName + " repeats its component");
            if (!grantedEffects.emplace(effect_key(address.route, address.target), grant->mode).second)
                return stop(EvaluationCaseStatus::HostError, std::string(FixtureMismatch) + "effect address for "
                    + address.scriptTypeName + "." + address.componentName + " repeats another target");
        }
        return true;
    }

    bool run_frames() {
        for (std::size_t index = 0; index < testCase.frameTimes.size(); ++index) {
            if (!run_frame_at(index))
                return false;
        }
        return true;
    }

    bool run_frame_at(std::size_t index) {
        EvaluationFrameContext context{index, testCase.frameTimes[index], &testCase.inputs, nullptr, nullptr, {}};
        if (!guarded("input action", [&] { return apply_input(context); })
            || !guarded("adapter delivery", [&] { return deliver(context.now); })
            || !advance(context.now))
            return false;
        return guarded("frame evidence", [&] { return observe(context); })
            && guarded("expectation", [&] { return expect(context); });
    }

    bool unchanged_frame(FrameNumber before, std::string_view what) {
        if (prepared.runtime && prepared.runner && prepared.runtime->frame() == before)
            return true;
        return stop(EvaluationCaseStatus::HostError, std::string(what) + " changed the evaluation frame or world");
    }

    bool apply_input(const EvaluationFrameContext& context) {
        if (!testCase.input)
            return true;
        const FrameNumber before = prepared.runtime->frame();
        testCase.input(prepared, context);
        return unchanged_frame(before, "input action");
    }

    bool deliver(IntentTime now) {
        for (const std::shared_ptr<simulation::InMemoryAdapter>& adapter : prepared.adapters) {
            if (!adapter)
                return stop(EvaluationCaseStatus::HostError, "case factory returned a null adapter");
            (void)adapter->deliver_through(now);
        }
        return true;
    }

    // One real Runtime::run_frame; a throw is a frame that did not complete.
    bool advance(IntentTime now) {
        std::string failure;
        try {
            FrameInput input;
            input.now = now;
            lastFrame = prepared.runtime->run_frame(std::move(input));
            return true;
        } catch (const std::exception& exception) {
            failure = exception.what();
        } catch (...) {
            failure = "unknown exception";
        }
        EvaluationFrameSummary summary;
        summary.now = now;
        result.frames.push_back(std::move(summary));
        result.framesRun = result.frames.size();
        return stop(EvaluationCaseStatus::HostError, "evaluation frame failed: " + failure);
    }

    const LuaExecutionResult* lifecycle_result() const {
        try {
            return prepared.runtime->world().get_system<scripting::LuaLifecycleSystem>().last_result(prepared.candidate);
        } catch (const std::exception&) {
            return nullptr;
        }
    }

    void collect_records() {
        std::vector<EventRecord> all = store.read_all();
        const std::size_t first = std::min(recordsSeen, all.size());
        frameRecords.assign(
            std::make_move_iterator(all.begin() + static_cast<std::ptrdiff_t>(first)),
            std::make_move_iterator(all.end()));
        recordsSeen = all.size();
    }

    EvaluationFrameSummary summarize(const FrameResult& frame, IntentTime now) const {
        EvaluationFrameSummary summary;
        summary.now = now;
        summary.frameCompleted = frame.frame.completed;
        if (lastLifecycle) {
            summary.lifecycleStatus = lastLifecycle->status;
            summary.createdIntents = lastLifecycle->createdIntents.size();
            summary.cancelledIntents = lastLifecycle->cancelledIntents.size();
        }
        const std::vector<IntentId> live = prepared.runtime->world().intents_owned_by(prepared.candidate);
        summary.liveCandidateIntents = live.size();
        summary.candidateSelected = selected_any(frame.frame, live);
        // Only the candidate's granted targets; observations need Read.
        for (const EffectCommand& command : frame.commands) {
            if (granted_mode(command.effect.adapterRoute, command.effect.target))
                ++summary.commandsIssued;
        }
        for (const EffectReport& report : frame.reports) {
            if (granted_mode(report.adapterRoute, report.target))
                summary.reportStatuses.push_back(report.status);
        }
        for (const ExternalObservation& observation : frame.observations) {
            const std::optional<ComponentAccessMode> mode = granted_mode(observation.adapterRoute, observation.target);
            if (mode && *mode != ComponentAccessMode::Write)
                ++summary.observations;
        }
        return summary;
    }

    static std::pair<std::string, std::string> effect_key(const AdapterRoute& route, const EffectTarget& target) {
        return {route.value(), target.value()};
    }

    // Undeclared targets are outside the candidate's scope.
    std::optional<ComponentAccessMode> granted_mode(const AdapterRoute& route, const EffectTarget& target) const {
        const auto found = grantedEffects.find(effect_key(route, target));
        if (found == grantedEffects.end())
            return std::nullopt;
        return found->second;
    }

    // Steps 4 and 5: lifecycle result and new records, then budget,
    // completion and lifecycle checks.
    bool observe(EvaluationFrameContext& context) {
        const FrameResult& frame = *lastFrame;
        lastLifecycle.reset();
        if (const LuaExecutionResult* lifecycle = lifecycle_result())
            lastLifecycle = *lifecycle;
        collect_records();
        result.frames.push_back(summarize(frame, context.now));
        result.framesRun = result.frames.size();
        context.frame = &frame;
        context.lifecycle = lastLifecycle ? &*lastLifecycle : nullptr;
        context.frameRecords = frameRecords;
        return check_frame(frame, context.frameIndex);
    }

    bool check_frame(const FrameResult& frame, std::size_t index) {
        const std::string at = "frame " + std::to_string(index) + ": ";
        if (store.exhausted())
            return stop(EvaluationCaseStatus::LimitExceeded, BudgetExhausted);
        if (!frame.frame.completed || prepared.runtime->faulted())
            return stop(EvaluationCaseStatus::HostError, at + "frame did not complete: " + frame.frame.failure_message);
        if (!lastLifecycle)
            return stop(EvaluationCaseStatus::HostError, at + "no lifecycle result for the candidate");
        const EvaluationCaseStatus status = lifecycle_case_status(lastLifecycle->status);
        if (status != EvaluationCaseStatus::Passed)
            return stop(status, at + "lifecycle failed: " + lastLifecycle->diagnostic);
        return true;
    }

    bool expect(const EvaluationFrameContext& context) {
        const FrameNumber before = prepared.runtime->frame();
        const EvaluationAssertion assertion = testCase.expect(prepared, context);
        if (!unchanged_frame(before, "expectation"))
            return false;
        result.frames.back().assertionPassed = assertion.passed;
        if (!assertion.passed)
            return stop(
                EvaluationCaseStatus::Failed,
                "assertion failed at frame " + std::to_string(context.frameIndex) + ": " + assertion.diagnostic);
        return true;
    }

    // Grant order; Write-only targets never expose a value.
    EvaluationTargetSummary target_summary(const ScopeGrant& grant) {
        EvaluationTargetSummary target{grant.scriptTypeName, grant.componentName, grant.mode, std::nullopt};
        if (grant.mode == ComponentAccessMode::Write)
            return target;
        const std::optional<LuaValue> value = prepared.runner->snapshot(
            prepared.runtime->world(), prepared.candidate, LuaExecutionResult::Watch{grant.scriptTypeName, grant.componentName});
        if (!value)
            throw std::runtime_error("readable target " + grant.scriptTypeName + "." + grant.componentName + " is unavailable");
        target.value = to_value(*value);
        return target;
    }

    void capture_final_state() {
        const bool captured = guarded("final state", [this] {
            for (const ScopeGrant& grant : run.grants)
                result.finalState.push_back(target_summary(grant));
            return true;
        });
        if (!captured)
            result.finalState.clear();
    }

    const EvaluationRun& run;
    const EvaluationCase& testCase;
    // Declared before `prepared`: the Runtime holds a pointer to it.
    BoundedEvaluationStore store;
    PreparedEvaluation prepared;
    EvaluationCaseResult result;
    std::size_t recordsSeen = 0;
    std::optional<FrameResult> lastFrame;
    std::optional<LuaExecutionResult> lastLifecycle;
    std::vector<EventRecord> frameRecords;
    std::map<std::pair<std::string, std::string>, ComponentAccessMode> grantedEffects;
};

EvaluationCaseResult not_run(const EvaluationCase& testCase) {
    EvaluationCaseResult result;
    result.name = testCase.name;
    result.status = EvaluationCaseStatus::NotRun;
    result.framesScheduled = testCase.frameTimes.size();
    return result;
}

// ---- Response accounting ----

// Every record field and collection element costs LogicalElementBytes on top of its
// content, as in the session's scope and proposal charges.

std::size_t frame_bytes(const EvaluationFrameSummary& frame) {
    // 11 fields: now, created, cancelled, live, commands and observations are
    // scalars; completed, selected and assertion are booleans; the lifecycle
    // optional is a presence flag plus a scalar enum when present; each report
    // status is one element holding an enum.
    constexpr std::size_t Fields = 11;
    std::size_t bytes = Fields * LogicalElementBytes + 6 * LogicalScalarBytes + 3 * LogicalFlagBytes + LogicalFlagBytes;
    if (frame.lifecycleStatus)
        bytes += LogicalScalarBytes;
    return saturating_add(bytes, elements_bytes(frame.reportStatuses.size(), LogicalElementBytes + LogicalScalarBytes));
}

std::size_t target_bytes(const EvaluationTargetSummary& target) {
    // 4 fields: two names, the mode enum and the value optional's presence flag.
    constexpr std::size_t Fields = 4;
    std::size_t bytes = saturating_add(target.scriptTypeName.size(), target.componentName.size());
    bytes = saturating_add(bytes, Fields * LogicalElementBytes + LogicalScalarBytes + LogicalFlagBytes);
    return target.value ? saturating_add(bytes, logical_bytes(*target.value)) : bytes;
}

std::size_t case_bytes(const EvaluationCaseResult& result) {
    // 7 fields: name, status, scheduled and run counts, diagnostic, frames and
    // final state.
    constexpr std::size_t Fields = 7;
    std::size_t bytes = saturating_add(result.name.size(), Fields * LogicalElementBytes + 3 * LogicalScalarBytes);
    bytes = saturating_add(bytes, result.diagnostic.size());
    for (const EvaluationFrameSummary& frame : result.frames)
        bytes = saturating_add(bytes, saturating_add(LogicalElementBytes, frame_bytes(frame)));
    for (const EvaluationTargetSummary& target : result.finalState)
        bytes = saturating_add(bytes, saturating_add(LogicalElementBytes, target_bytes(target)));
    return bytes;
}

std::size_t record_bytes(const EvaluationRecord& record) {
    std::size_t bytes = record_header_bytes(record.fixture.stableName);
    for (const EvaluationCaseResult& result : record.cases)
        bytes = saturating_add(bytes, saturating_add(LogicalElementBytes, case_bytes(result)));
    return bytes;
}

std::size_t collection_bytes(const Value& value) {
    std::size_t bytes = 0;
    if (value.kind() == Value::Kind::Array) {
        for (const Value& item : value.as_array())
            bytes = saturating_add(bytes, saturating_add(LogicalScalarBytes, logical_bytes(item)));
        return bytes;
    }
    for (const auto& [key, item] : value.as_object())
        bytes = saturating_add(bytes, saturating_add(LogicalScalarBytes + key.size(), logical_bytes(item)));
    return bytes;
}

}

std::size_t logical_bytes(const Value& value) {
    if (value.kind() == Value::Kind::Array || value.kind() == Value::Kind::Object)
        return collection_bytes(value);
    return scalar_bytes(value);
}

std::size_t record_header_bytes(std::string_view fixtureName) {
    // 9 fields: id, session (8 bytes, as every ID), proposal, scope, revision
    // and status are scalars; complete is a boolean; the fixture is 2 fields
    // (name bytes, version scalar); cases add their elements in record_bytes.
    constexpr std::size_t Fields = 9;
    constexpr std::size_t FixtureFields = 2;
    constexpr std::size_t Fixed = (Fields + FixtureFields) * LogicalElementBytes + 7 * LogicalScalarBytes + LogicalFlagBytes;
    return saturating_add(Fixed, fixtureName.size());
}

EvaluationOutcome run_evaluation(const EvaluationRun& run) {
    EvaluationOutcome outcome;
    outcome.status = EvaluationStatus::Passed;
    EvaluationRunBudget budget{run.limits.maxEvaluationRecords, run.limits.maxEvaluationRecordBytes};
    for (std::size_t index = 0; index < run.suite.cases.size(); ++index) {
        const EvaluationCase& testCase = run.suite.cases[index];
        if (outcome.status != EvaluationStatus::Passed) {
            outcome.cases.push_back(not_run(testCase));
            continue;
        }
        CaseExecution execution(run, testCase, budget);
        outcome.cases.push_back(execution.execute());
        append_trace(index, execution.records(), outcome.trace);
        outcome.status = record_status(outcome.cases.back().status);
    }
    outcome.storeBytes = budget.bytes;
    return outcome;
}

std::size_t bound_response(EvaluationRecord& record, std::size_t limit) {
    std::size_t bytes = record_bytes(record);
    if (bytes <= limit) {
        record.complete = true;
        return bytes;
    }
    // Never a partial success: evidence goes first, then every case result.
    record.complete = false;
    record.status = EvaluationStatus::LimitExceeded;
    for (EvaluationCaseResult& result : record.cases) {
        result.diagnostic.clear();
        result.frames.clear();
        result.finalState.clear();
    }
    bytes = record_bytes(record);
    if (bytes > limit) {
        record.cases.clear();
        bytes = record_bytes(record);
    }
    return bytes;
}

EvaluationWorldTotals world_totals(const EvaluationRecord& record, const std::vector<EvaluationTraceEntry>& trace) {
    EvaluationWorldTotals totals{record.id, false, {}};
    std::size_t cases = record.cases.size();
    for (const EvaluationTraceEntry& entry : trace)
        cases = std::max(cases, entry.caseIndex + 1);
    totals.cases.resize(cases);
    // Whether each case has an open frame; commands, reports and observations
    // are only recorded inside Runtime::run_frame.
    std::vector<bool> open(cases, false);
    const auto accepted = [](const Value& payload) {
        const auto found = payload.as_object().find("disposition");
        return found != payload.as_object().end() && found->second == Value{"accepted"};
    };
    for (const EvaluationTraceEntry& entry : trace) {
        std::vector<EvaluationWorldFrameTotals>& frames = totals.cases[entry.caseIndex].frames;
        if (entry.type == EventType::FrameStarted) {
            frames.push_back(EvaluationWorldFrameTotals{entry.payload.as_object().at("now").as_unsigned_integer(), 0, 0, 0});
            open[entry.caseIndex] = true;
        } else if (!open[entry.caseIndex]) {
            continue;
        } else if (entry.type == EventType::FrameCompleted || entry.type == EventType::FrameFailed) {
            open[entry.caseIndex] = false;
        } else if (entry.type == EventType::CommandIssued) {
            ++frames.back().commandsIssued;
        } else if (entry.type == EventType::ReportReceived && accepted(entry.payload)) {
            ++frames.back().reports;
        } else if (entry.type == EventType::ExternalObservationReceived && accepted(entry.payload)) {
            ++frames.back().observations;
        }
    }
    // Complete only when the kept record still states how many frames each
    // case ran and the trace holds every one of them, closed. A refused append
    // leaves its frame open or missing; before any frame it can only be the
    // case setup, which no other limit stops.
    totals.complete = !record.cases.empty() && cases == record.cases.size();
    for (std::size_t index = 0; totals.complete && index < cases; ++index) {
        const EvaluationCaseResult& result = record.cases[index];
        totals.complete = !open[index] && totals.cases[index].frames.size() == result.framesRun
            && !(result.status == EvaluationCaseStatus::LimitExceeded && result.framesRun == 0);
    }
    return totals;
}

}
