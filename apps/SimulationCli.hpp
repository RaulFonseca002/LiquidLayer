#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace liquid::simulation {

enum class ExitCode : int {
    Success = 0,
    UsageError = 2,
    ScriptError = 3,
    HostError = 4
};

int run_cli(
    const std::vector<std::string>& arguments,
    std::ostream& output,
    std::ostream& error
);

}
