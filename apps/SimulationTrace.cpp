#include "SimulationTrace.hpp"

#include "SimulationInput.hpp"
#include "SimulationScenario.hpp"

#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace liquid::simulation {

namespace {

using scripting::LuaExecutionLimits;
using scripting::LuaExecutionResult;

std::string json_string(std::string_view value) {
    constexpr char HexDigits[] = "0123456789abcdef";
    std::string encoded{"\""};
    encoded.reserve(value.size() + 2);
    for (std::size_t index = 0; index < value.size();) {
        unsigned char byte = static_cast<unsigned char>(value[index]);
        if (byte == '\"') {
            encoded += "\\\"";
        } else if (byte == '\\') {
            encoded += "\\\\";
        } else if (byte >= 0x20 && byte <= 0x7e) {
            encoded += static_cast<char>(byte);
        } else {
            std::size_t length = 0;
            if (byte >= 0xc2 && byte <= 0xdf)
                length = 2;
            else if (byte >= 0xe0 && byte <= 0xef)
                length = 3;
            else if (byte >= 0xf0 && byte <= 0xf4)
                length = 4;

            bool valid = length != 0 && index + length <= value.size();
            for (std::size_t offset = 1; valid && offset < length; ++offset) {
                unsigned char continuation = static_cast<unsigned char>(value[index + offset]);
                valid = continuation >= 0x80 && continuation <= 0xbf;
            }
            if (valid && length == 3) {
                unsigned char second = static_cast<unsigned char>(value[index + 1]);
                valid = (byte != 0xe0 || second >= 0xa0) && (byte != 0xed || second <= 0x9f);
            }
            if (valid && length == 4) {
                unsigned char second = static_cast<unsigned char>(value[index + 1]);
                valid = (byte != 0xf0 || second >= 0x90) && (byte != 0xf4 || second <= 0x8f);
            }

            if (valid) {
                encoded.append(value.substr(index, length));
                index += length;
                continue;
            }
            encoded += "\\u00";
            encoded += HexDigits[byte >> 4];
            encoded += HexDigits[byte & 0x0f];
        }
        ++index;
    }
    encoded += '\"';
    return encoded;
}

template <typename Value>
std::string number_array(const std::vector<Value>& values) {
    std::ostringstream encoded;
    encoded << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0)
            encoded << ',';
        encoded << values[index];
    }
    encoded << ']';
    return encoded.str();
}

std::string string_array(const std::vector<std::string>& values) {
    std::ostringstream encoded;
    encoded << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0)
            encoded << ',';
        encoded << json_string(values[index]);
    }
    encoded << ']';
    return encoded.str();
}

std::string json_value(const Value& value) {
    switch (value.kind()) {
    case Value::Kind::Null: return "null";
    case Value::Kind::Boolean: return value.as_boolean() ? "true" : "false";
    case Value::Kind::SignedInteger:
        return std::to_string(value.as_signed_integer());
    case Value::Kind::UnsignedInteger:
        return std::to_string(value.as_unsigned_integer());
    case Value::Kind::Double: {
        std::ostringstream encoded;
        encoded << value.as_double();
        return encoded.str();
    }
    case Value::Kind::String: return json_string(value.as_string());
    case Value::Kind::Bytes: {
        std::ostringstream encoded;
        encoded << '"';
        constexpr char Hex[] = "0123456789abcdef";
        for (std::uint8_t byte : value.as_bytes()) {
            encoded << Hex[byte >> 4U] << Hex[byte & 0x0fU];
        }
        encoded << '"';
        return encoded.str();
    }
    case Value::Kind::Array: {
        std::ostringstream encoded;
        encoded << '[';
        bool first = true;
        for (const Value& child : value.as_array()) {
            if (!first) encoded << ',';
            first = false;
            encoded << json_value(child);
        }
        encoded << ']';
        return encoded.str();
    }
    case Value::Kind::Object: {
        std::ostringstream encoded;
        encoded << '{';
        bool first = true;
        for (const auto& [key, child] : value.as_object()) {
            if (!first) encoded << ',';
            first = false;
            encoded << json_string(key) << ':' << json_value(child);
        }
        encoded << '}';
        return encoded.str();
    }
    }
    throw std::logic_error("unknown Value kind");
}

std::string command_status_name(CommandStatus status) {
    switch (status) {
    case CommandStatus::Pending: return "pending";
    case CommandStatus::Applied: return "applied";
    case CommandStatus::Rejected: return "rejected";
    case CommandStatus::Failed: return "failed";
    case CommandStatus::TimedOut: return "timed-out";
    case CommandStatus::Superseded: return "superseded";
    case CommandStatus::Indeterminate: return "indeterminate";
    }
    return "unknown";
}

class TraceObserver : public SimulationObserver {
private:
    std::ostream* output;
    std::uint64_t nextSequence = 0;
    bool writeFailed = false;

    void emit(std::string_view event, const std::string& fields = {}) {
        if (writeFailed)
            return;
        *output << "{\"schema\":\"liquid.trace.v2\",\"seq\":" << nextSequence++
                << ",\"event\":" << json_string(event);
        if (!fields.empty())
            *output << ',' << fields;
        *output << "}\n" << std::flush;
        writeFailed = !*output;
    }

    static std::string intent_fields(FrameNumber frame, IntentTime now, const IntentSnapshot& intent) {
        std::ostringstream fields;
        fields << "\"frame\":" << frame
               << ",\"now_ms\":" << now
               << ",\"intent_id\":" << intent.id
               << ",\"owner_id\":" << intent.owner
               << ",\"type\":" << json_string(intent.typeName)
               << ",\"type_id\":" << intent.type
               << ",\"component\":" << json_string(intent.component)
               << ",\"name\":" << json_string(intent.name)
               << ",\"desired_brightness\":" << intent.brightness
               << ",\"priority\":" << json_string(priority_name(intent.priority))
               << ",\"lifetime\":" << json_string(lifetime_name(intent.lifetime.kind));
        if (intent.lifetime.kind == IntentLifetimeKind::UntilTime)
            fields << ",\"expires_at_ms\":" << intent.lifetime.expiresAt;
        return fields.str();
    }

    static std::string terminal_fields(const SimulationOutcome& outcome) {
        std::ostringstream fields;
        std::string_view result = outcome.status == SimulationStatus::Success
            ? "success"
            : outcome.status == SimulationStatus::ScriptError ? "script_error" : "host_error";
        fields << "\"outcome\":" << json_string(result)
               << ",\"script_status\":" << json_string(lua_status_name(outcome.script.status))
               << ",\"final_brightness\":" << outcome.finalBrightness
               << ",\"device_brightness\":";
        if (outcome.deviceBrightness)
            fields << *outcome.deviceBrightness;
        else
            fields << "null";
        fields << ",\"commands_issued\":" << outcome.commandsIssued
               << ",\"reports_applied\":" << outcome.reportsApplied
               << ",\"observations_applied\":" << outcome.observationsApplied
               << ",\"tracking_system_runs\":" << outcome.trackingSystemRuns
               << ",\"frames_completed\":" << outcome.framesCompleted
               << ",\"faulted\":" << (outcome.faulted ? "true" : "false");
        return fields.str();
    }

public:
    explicit TraceObserver(std::ostream& destination) : output(&destination) {}

    bool failed() const { return writeFailed; }

    void run_started(const SimulationOptions& options) override {
        std::ostringstream fields;
        fields << "\"initial_brightness\":" << options.initialBrightness
               << ",\"frame_times\":" << number_array(options.frameTimes)
               << ",\"feedback_timing\":" << json_string(
                    options.feedbackTiming == FeedbackTiming::Deferred
                        ? "deferred" : "immediate")
               << ",\"latency_ms\":" << options.latencyMs
               << ",\"adapter_outcome\":" << json_string(
                    command_status_name(options.adapterOutcome))
               << ",\"duplicate_reports\":" << options.duplicateReports
               << ",\"silent\":" << (options.silent ? "true" : "false")
               << ",\"reverse_delivery\":"
               << (options.reverseDelivery ? "true" : "false");
        emit("run_started", fields.str());
    }

    void world_ready(
        BehaviorId behavior,
        ComponentTypeId lightType,
        ComponentSlotId lightSlot,
        int actualBrightness
    ) override {
        std::ostringstream fields;
        fields << "\"behavior_id\":" << behavior
               << ",\"light_type_id\":" << lightType
               << ",\"light_slot_id\":" << lightSlot
               << ",\"actual_brightness\":" << actualBrightness;
        emit("world_ready", fields.str());
    }

    void frame_started(FrameNumber frame, IntentTime now) override {
        emit("frame_started", "\"frame\":" + std::to_string(frame) +
            ",\"now_ms\":" + std::to_string(now));
    }

    void script_started(FrameNumber frame, IntentTime now) override {
        emit("script_started", "\"frame\":" + std::to_string(frame) +
            ",\"now_ms\":" + std::to_string(now));
    }

    void script_finished(
        FrameNumber frame,
        IntentTime now,
        const LuaExecutionResult& result
    ) override {
        std::ostringstream fields;
        fields << "\"frame\":" << frame
               << ",\"now_ms\":" << now
               << ",\"status\":" << json_string(lua_status_name(result.status))
               << ",\"created_intents\":" << result.createdIntents.size()
               << ",\"intent_ids\":" << number_array(result.createdIntents)
               << ",\"diagnostic\":" << json_string(result.diagnostic);
        emit("script_finished", fields.str());
    }

    void intent_created(FrameNumber frame, IntentTime now, const IntentSnapshot& intent) override {
        emit("desire_created", intent_fields(frame, now, intent));
    }

    void frame_completed(const FrameResult& result) override {
        const FrameLog& frame = result.frame;
        std::ostringstream fields;
        fields << "\"frame\":" << frame.frame
               << ",\"now_ms\":" << frame.now
               << ",\"completed\":" << (frame.completed ? "true" : "false")
               << ",\"phases\":" << string_array(frame.phases)
               << ",\"expired_intents\":" << frame.expired_intents
               << ",\"resolution_requests\":" << frame.resolution_requests
               << ",\"selected_intents\":" << frame.selected_intents
               << ",\"systems_completed\":" << frame.systems_run;
        emit("frame_completed", fields.str());
    }

    void intent_selected(FrameNumber frame, IntentTime now, const IntentSnapshot& intent) override {
        emit("desire_selected", intent_fields(frame, now, intent));
    }

    void runtime_record(const EventRecord& record) override {
        std::string_view event;
        switch (record.type) {
        case EventType::CommandIssued: event = "command_issued"; break;
        case EventType::CommandAttempted: event = "command_attempt"; break;
        case EventType::ReportReceived: event = "command_result"; break;
        case EventType::ObservedStateChanged: event = "observed_changed"; break;
        case EventType::ExternalObservationReceived:
            event = "external_observation"; break;
        case EventType::CommandStatusChanged: {
            const auto& payload = record.payload.as_object();
            const auto status = payload.find("status");
            if (status != payload.end() && status->second.kind() == Value::Kind::String &&
                status->second.as_string() == "timed-out") {
                event = "command_timeout";
            } else {
                event = "command_status";
            }
            break;
        }
        default: return;
        }
        std::ostringstream fields;
        fields << "\"record_seq\":" << record.sequence.value
               << ",\"record_type\":" << static_cast<std::uint32_t>(record.type)
               << ",\"data\":" << json_value(record.payload);
        emit(event, fields.str());

        if (record.type == EventType::CommandAttempted) {
            const auto& payload = record.payload.as_object();
            const auto attempt = payload.find("attempt");
            if (attempt != payload.end() &&
                attempt->second.kind() == Value::Kind::UnsignedInteger &&
                attempt->second.as_unsigned_integer() > 1) {
                emit("command_retry", fields.str());
            }
        }
    }

    void component_snapshot(
        FrameNumber frame,
        IntentTime now,
        int actualBrightness,
        std::optional<int> deviceBrightness
    ) override {
        std::ostringstream fields;
        fields << "\"frame\":" << frame
               << ",\"now_ms\":" << now
               << ",\"component\":\"officeLight\""
               << ",\"actual_brightness\":" << actualBrightness
               << ",\"device_brightness\":";
        if (deviceBrightness)
            fields << *deviceBrightness;
        else
            fields << "null";
        emit("component_snapshot", fields.str());
    }

    void intent_disappeared(FrameNumber frame, IntentTime now, IntentId id) override {
        std::ostringstream fields;
        fields << "\"frame\":" << frame
               << ",\"now_ms\":" << now
               << ",\"intent_id\":" << id;
        emit("desire_disappeared", fields.str());
    }

    void run_completed(const SimulationOutcome& outcome) override {
        emit("run_completed", terminal_fields(outcome));
    }

    void run_failed(const SimulationOutcome& outcome) override {
        std::string diagnostic = outcome.diagnostic.substr(0, 4096);
        std::string fields = terminal_fields(outcome);
        if (!outcome.failurePhase.empty())
            fields += ",\"failure_phase\":" + json_string(outcome.failurePhase);
        fields += ",\"diagnostic\":" + json_string(diagnostic);
        emit("run_failed", fields);
    }
};

void write_usage_error(std::ostream& error, const std::string& diagnostic) {
    error << "error: " << diagnostic << '\n';
    error << "Try 'liquid_sim_trace --help' for usage.\n";
}

}

int run_trace_cli(
    const std::vector<std::string>& arguments,
    std::ostream& output,
    std::ostream& error
) {
    SimulationParseResult parsed = parse_simulation_arguments(arguments);
    if (!parsed.succeeded) {
        write_usage_error(error, parsed.diagnostic);
        return static_cast<int>(ExitCode::UsageError);
    }
    if (parsed.helpRequested) {
        output << simulation_usage("liquid_sim_trace");
        if (!output) {
            error << "error: host failure: \"could not write trace output\"\n";
            return static_cast<int>(ExitCode::HostError);
        }
        return static_cast<int>(ExitCode::Success);
    }

    try {
        std::string source;
        std::string diagnostic;
        LuaExecutionLimits limits;
        if (!read_simulation_script(parsed.options.scriptPath, limits.maxSourceBytes, source, diagnostic)) {
            write_usage_error(error, diagnostic);
            return static_cast<int>(ExitCode::UsageError);
        }

        TraceObserver observer(output);
        SimulationOutcome outcome = run_scenario(
            {
                parsed.options.initialBrightness,
                std::move(source),
                parsed.options.frameTimes,
                parsed.options.feedbackTiming,
                parsed.options.latencyMs,
                parsed.options.adapterOutcome,
                parsed.options.duplicateReports,
                parsed.options.silent,
                parsed.options.reverseDelivery
            },
            observer
        );
        if (observer.failed()) {
            error << "error: host failure: \"could not write trace output\"\n";
            return static_cast<int>(ExitCode::HostError);
        }
        return static_cast<int>(exit_code_for(outcome.status));
    } catch (const std::exception& exception) {
        error << "error: host failure: \"" << escape_text(exception.what()) << "\"\n";
        return static_cast<int>(ExitCode::HostError);
    } catch (...) {
        error << "error: host failure: \"unknown exception\"\n";
        return static_cast<int>(ExitCode::HostError);
    }
}

}
