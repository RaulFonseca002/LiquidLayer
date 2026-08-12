#include "SimulationCli.hpp"

#include "SimulationInput.hpp"
#include "SimulationScenario.hpp"

#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace liquid::simulation {

namespace {

using scripting::LuaExecutionLimits;
using scripting::LuaExecutionResult;
using scripting::LuaExecutionStatus;

template <typename Value>
std::string join_values(const std::vector<Value>& values) {
    std::ostringstream joined;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0)
            joined << ',';
        joined << values[index];
    }
    return joined.str();
}

std::string join_ids(const std::vector<IntentId>& ids) {
    return ids.empty() ? "-" : join_values(ids);
}

class TextObserver : public SimulationObserver {
private:
    std::ostream* output;

    void write_final(const SimulationOutcome& outcome) {
        *output << "final component=Light.officeLight brightness=" << outcome.finalBrightness
                << " tracking_system_runs=" << outcome.trackingSystemRuns
                << " frames_completed=" << outcome.framesCompleted
                << " faulted=" << (outcome.faulted ? "true" : "false") << '\n';
    }

public:
    explicit TextObserver(std::ostream& destination) : output(&destination) {}

    void run_started(const SimulationOptions& options) override {
        *output << "scenario initial_brightness=" << options.initialBrightness
                << " frame_count=" << options.frameTimes.size() << '\n';
    }

    void script_finished(
        FrameNumber,
        IntentTime,
        const LuaExecutionResult& result
    ) override {
        *output << "script status=" << lua_status_name(result.status)
                << " created_intents=" << result.createdIntents.size()
                << " intent_ids=" << join_ids(result.createdIntents)
                << " diagnostic=\"" << escape_text(result.diagnostic) << "\"\n";
    }

    void frame_completed(const FrameLog& frame) override {
        *output << "frame number=" << frame.frame
                << " now_ms=" << frame.now
                << " completed=" << (frame.completed ? "true" : "false")
                << " phases=" << join_values(frame.phases)
                << " expired_intents=" << frame.expired_intents
                << " resolution_requests=" << frame.resolution_requests
                << " selected_intents=" << frame.selected_intents
                << " systems_completed=" << frame.systems_run << '\n';
    }

    void intent_selected(FrameNumber frame, IntentTime, const IntentSnapshot& intent) override {
        *output << "selection frame=" << frame
                << " type=" << intent.typeName
                << " type_id=" << intent.type
                << " component=" << escape_text(intent.component)
                << " intent_id=" << intent.id
                << " brightness=" << intent.brightness
                << " priority=" << priority_name(intent.priority)
                << " lifetime=" << lifetime_name(intent.lifetime.kind);
        if (intent.lifetime.kind == IntentLifetimeKind::UntilTime)
            *output << " expires_at_ms=" << intent.lifetime.expiresAt;
        *output << '\n';
    }

    void run_completed(const SimulationOutcome& outcome) override { write_final(outcome); }
    void run_failed(const SimulationOutcome& outcome) override {
        if (outcome.framesCompleted != 0)
            write_final(outcome);
    }
};

void write_usage_error(std::ostream& error, const std::string& diagnostic) {
    error << "error: " << diagnostic << '\n';
    error << "Try 'liquid_sim_cli --help' for usage.\n";
}

}

int run_cli(
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
        output << simulation_usage("liquid_sim_cli");
        if (!output) {
            error << "error: host failure: \"could not write simulation output\"\n";
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

        TextObserver observer(output);
        SimulationOutcome outcome = run_scenario(
            {parsed.options.initialBrightness, std::move(source), parsed.options.frameTimes},
            observer
        );
        if (!output) {
            error << "error: host failure: \"could not write simulation output\"\n";
            return static_cast<int>(ExitCode::HostError);
        }
        if (outcome.status == SimulationStatus::HostError &&
            outcome.script.status != LuaExecutionStatus::HostError)
            error << "error: host failure: \"" << escape_text(outcome.diagnostic) << "\"\n";
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
