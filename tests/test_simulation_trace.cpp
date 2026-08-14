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
        output << "function on_start(frame)\n"
                  "    local light = access.Light.officeLight\n"
                  "    solid.watch(light)\n"
                  "    light.propose({ name = 'fallback', value = { brightness = 30 }, priority = 'low', lifetime = 'persistent' })\n"
                  "    light.propose({ name = 'boost', value = { brightness = 70 }, priority = 'high', duration_ms = 5 })\n"
                  "end\n";
        assert(output);
    }

    const std::vector<std::string> arguments{
        "--initial-brightness", "10", "--script", script.string(),
        "--frame-time", "100", "--frame-time", "105", "--frame-time", "110"
    };
    Invocation first = invoke(arguments);
    assert(first.status == static_cast<int>(ExitCode::Success));
    assert(first.error.empty());
    assert(first.output == invoke(arguments).output);

    std::vector<std::string> traceLines = lines(first.output);
    assert(traceLines.size() == 34);
    for (std::size_t index = 0; index < traceLines.size(); ++index) {
        assert(traceLines[index].starts_with(
            "{\"schema\":\"liquid.trace.v2\",\"seq\":" + std::to_string(index) + ","
        ));
    }
    // Frame 100: the boost is selected and commanded while the confirmed
    // component still reads 10.
    assert(traceLines[0].find("\"event\":\"run_started\"") != std::string::npos);
    assert(traceLines[5].find("\"event\":\"desire_created\"") != std::string::npos);
    assert(traceLines[5].find("\"desired_brightness\":30") != std::string::npos);
    assert(traceLines[6].find("\"desired_brightness\":70") != std::string::npos);
    assert(traceLines[7].find("\"event\":\"desire_selected\"") != std::string::npos);
    assert(traceLines[7].find("\"desired_brightness\":70") != std::string::npos);
    assert(traceLines[8].find("\"event\":\"command_issued\"") != std::string::npos);
    assert(traceLines[11].find("\"event\":\"component_snapshot\"") != std::string::npos);
    assert(traceLines[11].find("\"actual_brightness\":10") != std::string::npos);
    assert(traceLines[11].find("\"device_brightness\":70") != std::string::npos);
    // Frame 105: the applied report is authoritative before behavior logic,
    // the boost expires, and the fallback is commanded.
    assert(traceLines[15].find("\"event\":\"desire_disappeared\"") != std::string::npos);
    assert(traceLines[16].find("\"event\":\"desire_selected\"") != std::string::npos);
    assert(traceLines[16].find("\"desired_brightness\":30") != std::string::npos);
    assert(traceLines[17].find("\"event\":\"command_result\"") != std::string::npos);
    assert(traceLines[19].find("\"event\":\"observed_changed\"") != std::string::npos);
    assert(traceLines[19].find("\"observed\":70") != std::string::npos);
    assert(traceLines[23].find("\"actual_brightness\":70") != std::string::npos);
    assert(traceLines[23].find("\"device_brightness\":30") != std::string::npos);
    // Frame 110: the fallback report confirms 30 and desire matches state.
    assert(traceLines[30].find("\"event\":\"observed_changed\"") != std::string::npos);
    assert(traceLines[30].find("\"observed\":30") != std::string::npos);
    assert(traceLines[32].find("\"actual_brightness\":30") != std::string::npos);
    assert(traceLines[32].find("\"device_brightness\":30") != std::string::npos);
    assert(traceLines[33].find("\"event\":\"run_completed\"") != std::string::npos);
    assert(traceLines[33].find("\"final_brightness\":30") != std::string::npos);
    assert(traceLines[33].find("\"commands_issued\":2") != std::string::npos);
    assert(traceLines[33].find("\"reports_applied\":2") != std::string::npos);

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
