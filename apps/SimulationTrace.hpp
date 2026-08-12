#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace liquid::simulation {

int run_trace_cli(
    const std::vector<std::string>& arguments,
    std::ostream& output,
    std::ostream& error
);

}
