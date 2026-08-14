#include "SimulationInput.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace liquid::simulation {

namespace {

template <typename Integer>
bool parse_unsigned(std::string_view text, Integer& value) {
    if (text.empty())
        return false;

    Integer parsed = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc{} || result.ptr != end)
        return false;

    value = parsed;
    return true;
}

}

std::string simulation_usage(std::string_view executableName) {
    return "Usage: " + std::string(executableName) +
        " --initial-brightness <0..100> --script <lua-file> "
        "--frame-time <milliseconds> [--frame-time <milliseconds> ...] "
        "[--feedback-timing deferred|immediate] [--latency <milliseconds>] "
        "[--outcome applied|rejected|failed] [--duplicates <0..64>] "
        "[--silent true|false] [--reverse-delivery true|false]\n";
}

std::string escape_text(std::string_view value) {
    constexpr char HexDigits[] = "0123456789abcdef";
    std::string escaped;
    escaped.reserve(value.size());

    for (unsigned char character : value) {
        switch (character) {
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (character < 0x20 || character >= 0x7f) {
                escaped += "\\x";
                escaped += HexDigits[character >> 4];
                escaped += HexDigits[character & 0x0f];
            } else {
                escaped += static_cast<char>(character);
            }
        }
    }
    return escaped;
}

SimulationParseResult parse_simulation_arguments(const std::vector<std::string>& arguments) {
    SimulationParseResult result;
    if (arguments.size() == 1 && arguments.front() == "--help") {
        result.succeeded = true;
        result.helpRequested = true;
        return result;
    }

    bool hasBrightness = false;
    bool hasScript = false;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& option = arguments[index];
        if (option == "--help") {
            result.diagnostic = "--help cannot be combined with scenario options";
            return result;
        }
        if (option != "--initial-brightness" && option != "--script" &&
            option != "--frame-time" && option != "--feedback-timing" &&
            option != "--latency" && option != "--outcome" &&
            option != "--duplicates" && option != "--silent" &&
            option != "--reverse-delivery") {
            result.diagnostic = "unknown option: " + escape_text(option);
            return result;
        }
        if (index + 1 >= arguments.size()) {
            result.diagnostic = "missing value for " + option;
            return result;
        }

        const std::string& value = arguments[++index];
        if (option == "--initial-brightness") {
            if (hasBrightness) {
                result.diagnostic = "--initial-brightness may be specified only once";
                return result;
            }
            std::uint64_t brightness = 0;
            if (!parse_unsigned(value, brightness) || brightness > 100) {
                result.diagnostic = "initial brightness must be an integer from 0 to 100";
                return result;
            }
            result.options.initialBrightness = static_cast<int>(brightness);
            hasBrightness = true;
            continue;
        }

        if (option == "--script") {
            if (hasScript) {
                result.diagnostic = "--script may be specified only once";
                return result;
            }
            if (value.empty()) {
                result.diagnostic = "script path must not be empty";
                return result;
            }
            result.options.scriptPath = value;
            hasScript = true;
            continue;
        }

        if (option == "--feedback-timing") {
            if (value == "deferred")
                result.options.feedbackTiming = FeedbackTiming::Deferred;
            else if (value == "immediate")
                result.options.feedbackTiming = FeedbackTiming::Immediate;
            else {
                result.diagnostic = "feedback timing must be deferred or immediate";
                return result;
            }
            continue;
        }
        if (option == "--outcome") {
            if (value == "applied")
                result.options.adapterOutcome = CommandStatus::Applied;
            else if (value == "rejected")
                result.options.adapterOutcome = CommandStatus::Rejected;
            else if (value == "failed")
                result.options.adapterOutcome = CommandStatus::Failed;
            else {
                result.diagnostic = "outcome must be applied, rejected, or failed";
                return result;
            }
            continue;
        }
        if (option == "--silent" || option == "--reverse-delivery") {
            if (value != "true" && value != "false") {
                result.diagnostic = option + " must be true or false";
                return result;
            }
            const bool enabled = value == "true";
            if (option == "--silent")
                result.options.silent = enabled;
            else
                result.options.reverseDelivery = enabled;
            continue;
        }
        if (option == "--latency") {
            if (!parse_unsigned(value, result.options.latencyMs)) {
                result.diagnostic = "latency must be an unsigned integer";
                return result;
            }
            continue;
        }
        if (option == "--duplicates") {
            std::uint64_t duplicates = 0;
            if (!parse_unsigned(value, duplicates) || duplicates > 64) {
                result.diagnostic = "duplicates must be an integer from 0 to 64";
                return result;
            }
            result.options.duplicateReports = static_cast<std::size_t>(duplicates);
            continue;
        }

        IntentTime frameTime = 0;
        if (!parse_unsigned(value, frameTime)) {
            result.diagnostic = "frame time must be an unsigned integer in milliseconds";
            return result;
        }
        if (frameTime > static_cast<IntentTime>(std::numeric_limits<std::int64_t>::max())) {
            result.diagnostic = "frame time exceeds the Lua integer range";
            return result;
        }
        if (!result.options.frameTimes.empty() && frameTime < result.options.frameTimes.back()) {
            result.diagnostic = "frame times must be nondecreasing";
            return result;
        }
        result.options.frameTimes.push_back(frameTime);
    }

    if (!hasBrightness || !hasScript || result.options.frameTimes.empty()) {
        result.diagnostic = "missing required options";
        return result;
    }
    result.succeeded = true;
    return result;
}

bool read_simulation_script(
    const std::string& path,
    std::size_t maximumSourceBytes,
    std::string& source,
    std::string& diagnostic
) {
    std::ifstream script(path, std::ios::binary);
    if (!script) {
        diagnostic = "could not open script file";
        return false;
    }
    if (maximumSourceBytes == std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("Lua source limit cannot be represented");

    source.resize(maximumSourceBytes + 1);
    script.read(source.data(), static_cast<std::streamsize>(source.size()));
    source.resize(static_cast<std::size_t>(script.gcount()));
    if (script.bad())
        throw std::runtime_error("could not read script file");
    return true;
}

ExitCode exit_code_for(SimulationStatus status) {
    switch (status) {
    case SimulationStatus::Success: return ExitCode::Success;
    case SimulationStatus::ScriptError: return ExitCode::ScriptError;
    case SimulationStatus::HostError: return ExitCode::HostError;
    }
    return ExitCode::HostError;
}

}
