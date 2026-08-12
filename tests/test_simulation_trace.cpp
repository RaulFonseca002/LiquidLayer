#include "SimulationInput.hpp"
#include "SimulationTrace.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using liquid::simulation::ExitCode;
using liquid::simulation::run_trace_cli;

namespace {

struct Invocation {
    int status = 0;
    std::string output;
    std::string error;
};

class TemporaryDirectory {
private:
    std::filesystem::path pathValue;

public:
    TemporaryDirectory() {
        std::random_device random;
        const auto root = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 32; ++attempt) {
            pathValue = root / ("liquid-trace-test-" + std::to_string(random()));
            std::error_code error;
            if (std::filesystem::create_directory(pathValue, error))
                return;
        }
        assert(false && "could not create trace test directory");
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(pathValue, error);
    }

    const std::filesystem::path& path() const { return pathValue; }
};

Invocation invoke(const std::vector<std::string>& arguments) {
    std::ostringstream output;
    std::ostringstream error;
    int status = run_trace_cli(arguments, output, error);
    return {status, output.str(), error.str()};
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> result;
    std::istringstream input(text);
    for (std::string line; std::getline(input, line);)
        result.push_back(line);
    return result;
}

}

int main() {
    TemporaryDirectory ownedDirectory;
    const std::filesystem::path& directory = ownedDirectory.path();
    const std::filesystem::path script = directory / "scenario.lua";
    {
        std::ofstream output(script, std::ios::binary);
        output << "local light = access.Light.officeLight\n"
                  "light.propose({ value = { brightness = 30 }, priority = 'low' })\n"
                  "light.propose({ value = { brightness = 70 }, priority = 'high', duration_ms = 5 })\n";
        assert(output);
    }

    const std::vector<std::string> arguments{
        "--initial-brightness", "10", "--script", script.string(),
        "--frame-time", "100", "--frame-time", "105"
    };
    Invocation first = invoke(arguments);
    assert(first.status == static_cast<int>(ExitCode::Success));
    assert(first.error.empty());
    assert(first.output == invoke(arguments).output);

    std::vector<std::string> traceLines = lines(first.output);
    assert(traceLines.size() == 16);
    for (std::size_t index = 0; index < traceLines.size(); ++index) {
        assert(traceLines[index].starts_with(
            "{\"schema\":\"liquid.trace.v1\",\"seq\":" + std::to_string(index) + ","
        ));
    }
    assert(traceLines[0].find("\"event\":\"run_started\"") != std::string::npos);
    assert(traceLines[5].find("\"desired_brightness\":30") != std::string::npos);
    assert(traceLines[6].find("\"desired_brightness\":70") != std::string::npos);
    assert(traceLines[8].find("\"event\":\"intent_selected\"") != std::string::npos);
    assert(traceLines[8].find("\"desired_brightness\":70") != std::string::npos);
    assert(traceLines[9].find("\"actual_brightness\":10") != std::string::npos);
    assert(traceLines[12].find("\"event\":\"intent_disappeared\"") != std::string::npos);
    assert(traceLines[13].find("\"desired_brightness\":30") != std::string::npos);
    assert(traceLines[14].find("\"actual_brightness\":10") != std::string::npos);
    assert(traceLines[15].find("\"event\":\"run_completed\"") != std::string::npos);

    const std::filesystem::path failureScript = directory / "failure.lua";
    {
        std::ofstream output(failureScript, std::ios::binary);
        output << "error('bad\\nbyte café " << static_cast<char>(0x1b) << "')\n";
        assert(output);
    }
    Invocation failure = invoke({
        "--initial-brightness", "10", "--script", failureScript.string(), "--frame-time", "100"
    });
    assert(failure.status == static_cast<int>(ExitCode::ScriptError));
    assert(failure.error.empty());
    assert(failure.output.find("\\u000a") != std::string::npos);
    assert(failure.output.find("\\u001b") != std::string::npos);
    assert(failure.output.find("café") != std::string::npos);
    assert(failure.output.find("\\u00c3\\u00a9") == std::string::npos);
    assert(failure.output.find("\"outcome\":\"script_error\"") != std::string::npos);
    assert(failure.output.find("\"faulted\":false") != std::string::npos);

}
