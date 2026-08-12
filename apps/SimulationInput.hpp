#pragma once

#include "SimulationScenario.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace liquid::simulation {

enum class ExitCode : int {
    Success = 0,
    UsageError = 2,
    ScriptError = 3,
    HostError = 4
};

struct SimulationFileOptions {
    int initialBrightness = 0;
    std::string scriptPath;
    std::vector<IntentTime> frameTimes;
};

struct SimulationParseResult {
    SimulationFileOptions options;
    bool succeeded = false;
    bool helpRequested = false;
    std::string diagnostic;
};

std::string simulation_usage(std::string_view executableName);
std::string escape_text(std::string_view value);
SimulationParseResult parse_simulation_arguments(const std::vector<std::string>& arguments);
bool read_simulation_script(
    const std::string& path,
    std::size_t maximumSourceBytes,
    std::string& source,
    std::string& diagnostic
);
ExitCode exit_code_for(SimulationStatus status);

}
