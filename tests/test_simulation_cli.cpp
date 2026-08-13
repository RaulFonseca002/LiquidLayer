#include "SimulationCli.hpp"
#include "liquid/scripting/LuaBehaviorRunner.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace liquid;

using liquid::simulation::ExitCode;
using liquid::simulation::run_cli;

namespace {

class TemporaryDirectory {
private:
    std::filesystem::path directoryPath;
    bool owned = false;

public:
    TemporaryDirectory()
        : directoryPath(std::filesystem::current_path() / "test_simulation_cli_workspace") {
        std::error_code error;
        owned = std::filesystem::create_directory(directoryPath, error);
        REQUIRE(!error);
        REQUIRE(owned);
    }

    ~TemporaryDirectory() {
        if (!owned)
            return;

        std::error_code error;
        std::filesystem::remove_all(directoryPath, error);
    }

    std::string write_script(const std::string& name, const std::string& source) const {
        std::filesystem::path scriptPath = directoryPath / name;
        std::ofstream script(scriptPath, std::ios::binary);
        REQUIRE(script);
        script << source;
        REQUIRE(script);
        return scriptPath.string();
    }
};

struct Invocation {
    int status = 0;
    std::string output;
    std::string error;
};

Invocation invoke(const std::vector<std::string>& arguments) {
    std::ostringstream output;
    std::ostringstream error;
    int status = run_cli(arguments, output, error);
    return {status, output.str(), error.str()};
}

}

TEST_CASE("test_simulation_cli") {
    TemporaryDirectory temporaryDirectory;

    {
        Invocation help = invoke({"--help"});
        REQUIRE(help.status == static_cast<int>(ExitCode::Success));
        REQUIRE(help.output ==
            "Usage: liquid_sim_cli --initial-brightness <0..100> --script <lua-file> "
            "--frame-time <milliseconds> [--frame-time <milliseconds> ...]\n");
        REQUIRE(help.error.empty());
    }

    {
        Invocation missing = invoke({});
        REQUIRE(missing.status == static_cast<int>(ExitCode::UsageError));
        REQUIRE(missing.output.empty());
        REQUIRE(missing.error ==
            "error: missing required options\n"
            "Try 'liquid_sim_cli --help' for usage.\n");
    }

    std::string successScript = temporaryDirectory.write_script("success.lua", R"(
        local light = access.Light.officeLight
        light.propose({ value = { brightness = 30 }, priority = "low" })
        light.propose({ value = { brightness = 70 }, priority = "high", duration_ms = 5 })
    )");

    const std::vector<std::string> successArguments{
        "--initial-brightness", "10",
        "--script", successScript,
        "--frame-time", "100",
        "--frame-time", "105"
    };

    const std::string expectedSuccess =
        "scenario initial_brightness=10 frame_count=2\n"
        "script status=success created_intents=2 intent_ids=1,2 diagnostic=\"\"\n"
        "frame number=0 now_ms=100 completed=true phases=begin_frame,expire_intents,run_input_systems,run_behavior_systems,run_decision_systems,resolve_intents,end_frame expired_intents=0 resolution_requests=1 selected_intents=1 systems_completed=2\n"
        "selection frame=0 type=Light type_id=0 component=officeLight intent_id=2 brightness=70 priority=high lifetime=until_time expires_at_ms=105\n"
        "frame number=1 now_ms=105 completed=true phases=begin_frame,expire_intents,run_input_systems,run_behavior_systems,run_decision_systems,resolve_intents,end_frame expired_intents=1 resolution_requests=1 selected_intents=1 systems_completed=2\n"
        "selection frame=1 type=Light type_id=0 component=officeLight intent_id=1 brightness=30 priority=low lifetime=persistent\n"
        "final component=Light.officeLight brightness=10 tracking_system_runs=2 frames_completed=2 faulted=false\n";

    Invocation firstSuccess = invoke(successArguments);
    REQUIRE(firstSuccess.status == static_cast<int>(ExitCode::Success));
    REQUIRE(firstSuccess.output == expectedSuccess);
    REQUIRE(firstSuccess.error.empty());

    Invocation replay = invoke(successArguments);
    REQUIRE(replay.status == firstSuccess.status);
    REQUIRE(replay.output == firstSuccess.output);
    REQUIRE(replay.error == firstSuccess.error);

    std::string failureScript = temporaryDirectory.write_script(
        "failure.lua",
        "access.Light.officeLight.propose({ value = { brightness = 80 } })\n"
        "error(\"script\\nfailure\\194\\160\", 0)"
    );

    Invocation failure = invoke({
        "--initial-brightness", "10",
        "--script", failureScript,
        "--frame-time", "100"
    });

    REQUIRE(failure.status == static_cast<int>(ExitCode::ScriptError));
    REQUIRE(failure.output ==
        "scenario initial_brightness=10 frame_count=1\n"
        "script status=runtime_error created_intents=0 intent_ids=- diagnostic=\"script\\nfailure\\xc2\\xa0\"\n"
        "frame number=0 now_ms=100 completed=true phases=begin_frame,expire_intents,run_input_systems,run_behavior_systems,run_decision_systems,resolve_intents,end_frame expired_intents=0 resolution_requests=1 selected_intents=0 systems_completed=2\n"
        "final component=Light.officeLight brightness=10 tracking_system_runs=1 frames_completed=1 faulted=false\n");
    REQUIRE(failure.error.empty());

    Invocation failureReplay = invoke({
        "--initial-brightness", "10",
        "--script", failureScript,
        "--frame-time", "100"
    });
    REQUIRE(failureReplay.status == failure.status);
    REQUIRE(failureReplay.output == failure.output);
    REQUIRE(failureReplay.error == failure.error);

    {
        Invocation invalidBrightness = invoke({
            "--initial-brightness", "101",
            "--script", successScript,
            "--frame-time", "100"
        });
        REQUIRE(invalidBrightness.status == static_cast<int>(ExitCode::UsageError));
        REQUIRE(invalidBrightness.output.empty());
        REQUIRE(invalidBrightness.error ==
            "error: initial brightness must be an integer from 0 to 100\n"
            "Try 'liquid_sim_cli --help' for usage.\n");
    }

    {
        Invocation decreasingTime = invoke({
            "--initial-brightness", "10",
            "--script", successScript,
            "--frame-time", "100",
            "--frame-time", "99"
        });
        REQUIRE(decreasingTime.status == static_cast<int>(ExitCode::UsageError));
        REQUIRE(decreasingTime.output.empty());
        REQUIRE(decreasingTime.error ==
            "error: frame times must be nondecreasing\n"
            "Try 'liquid_sim_cli --help' for usage.\n");
    }

    {
        Invocation missingScript = invoke({
            "--initial-brightness", "10",
            "--script", "missing_simulation_script.lua",
            "--frame-time", "100"
        });
        REQUIRE(missingScript.status == static_cast<int>(ExitCode::UsageError));
        REQUIRE(missingScript.output.empty());
        REQUIRE(missingScript.error ==
            "error: could not open script file\n"
            "Try 'liquid_sim_cli --help' for usage.\n");
    }

    {
        Invocation unknownOption = invoke({"--unknown"});
        REQUIRE(unknownOption.status == static_cast<int>(ExitCode::UsageError));
        REQUIRE(unknownOption.output.empty());
        REQUIRE(unknownOption.error ==
            "error: unknown option: --unknown\n"
            "Try 'liquid_sim_cli --help' for usage.\n");
    }

    {
        Invocation missingValue = invoke({"--script"});
        REQUIRE(missingValue.status == static_cast<int>(ExitCode::UsageError));
        REQUIRE(missingValue.output.empty());
        REQUIRE(missingValue.error ==
            "error: missing value for --script\n"
            "Try 'liquid_sim_cli --help' for usage.\n");
    }

    {
        std::uint64_t outsideLuaRange =
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1;
        Invocation invalidTime = invoke({
            "--initial-brightness", "10",
            "--script", successScript,
            "--frame-time", std::to_string(outsideLuaRange)
        });
        REQUIRE(invalidTime.status == static_cast<int>(ExitCode::UsageError));
        REQUIRE(invalidTime.output.empty());
        REQUIRE(invalidTime.error ==
            "error: frame time exceeds the Lua integer range\n"
            "Try 'liquid_sim_cli --help' for usage.\n");
    }

    {
        liquid::scripting::LuaExecutionLimits limits;
        std::string oversizedSource(limits.maxSourceBytes + 1, 'x');
        std::string oversizedScript = temporaryDirectory.write_script("oversized.lua", oversizedSource);
        Invocation oversized = invoke({
            "--initial-brightness", "10",
            "--script", oversizedScript,
            "--frame-time", "100"
        });

        REQUIRE(oversized.status == static_cast<int>(ExitCode::ScriptError));
        REQUIRE(oversized.output ==
            "scenario initial_brightness=10 frame_count=1\n"
            "script status=source_limit_exceeded created_intents=0 intent_ids=- diagnostic=\"Lua source exceeds size limit\"\n"
            "frame number=0 now_ms=100 completed=true phases=begin_frame,expire_intents,run_input_systems,run_behavior_systems,run_decision_systems,resolve_intents,end_frame expired_intents=0 resolution_requests=1 selected_intents=0 systems_completed=2\n"
            "final component=Light.officeLight brightness=10 tracking_system_runs=1 frames_completed=1 faulted=false\n");
        REQUIRE(oversized.error.empty());
    }

    {
        std::ostringstream failedOutput;
        failedOutput.setstate(std::ios::badbit);
        std::ostringstream error;
        int status = run_cli({"--help"}, failedOutput, error);
        REQUIRE(status == static_cast<int>(ExitCode::HostError));
        REQUIRE(error.str() == "error: host failure: \"could not write simulation output\"\n");
    }

}
