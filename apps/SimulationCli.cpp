#include "SimulationCli.hpp"

#include "liquid/Runtime.hpp"
#include "liquid/scripting/LuaBehaviorRunner.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace liquid::simulation {

namespace {

using scripting::LuaBehaviorRunner;
using scripting::LuaComponentCodec;
using scripting::LuaExecutionLimits;
using scripting::LuaExecutionResult;
using scripting::LuaExecutionStatus;
using scripting::LuaValue;

constexpr std::string_view Usage =
    "Usage: liquid_sim_cli --initial-brightness <0..100> --script <lua-file> "
    "--frame-time <milliseconds> [--frame-time <milliseconds> ...]\n";
constexpr char LightTypeName[] = "Light";
constexpr char LightComponentName[] = "officeLight";

struct Light {
    int brightness = 0;
};

struct Options {
    int initialBrightness = 0;
    std::string scriptPath;
    std::vector<IntentTime> frameTimes;
};

struct ParseResult {
    Options options;
    bool succeeded = false;
    bool helpRequested = false;
    std::string diagnostic;
};

std::string escape_cli_text(std::string_view value) {
    constexpr char HexDigits[] = "0123456789abcdef";
    std::string escaped;
    escaped.reserve(value.size());

    for (const char textCharacter : value) {
        const unsigned char character =
            static_cast<unsigned char>(textCharacter);
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (character < 0x20 || character > 0x7e) {
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

ParseResult parse_arguments(const std::vector<std::string>& arguments) {
    ParseResult result;

    if (arguments.size() == 1 && arguments[0] == "--help") {
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

        if (option != "--initial-brightness" && option != "--script" && option != "--frame-time") {
            result.diagnostic = "unknown option: " + escape_cli_text(option);
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

bool read_script(
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

LuaComponentCodec<Light> light_codec() {
    return {
        [](const Light& light) {
            return LuaValue::Table{{"brightness", LuaValue{light.brightness}}};
        },
        [](const LuaValue& value) {
            const auto& table = value.as_table();

            if (table.size() != 1 || !table.contains("brightness"))
                throw std::runtime_error("Light requires exactly brightness");

            std::int64_t brightness = table.at("brightness").as_integer();
            if (brightness < 0 || brightness > 100)
                throw std::runtime_error("brightness must be between 0 and 100");

            return Light{static_cast<int>(brightness)};
        }
    };
}

class LuaScenarioSystem : public System {
public:
    static constexpr std::string_view stableName = "apps.SimulationCli.cpp.LuaScenarioSystem";
    static constexpr std::uint32_t version = 1;
private:
    LuaBehaviorRunner* runner;
    BehaviorId owner;
    std::string source;
    bool hasRun = false;
    LuaExecutionResult executionResult;

public:
    LuaScenarioSystem(LuaBehaviorRunner& behaviorRunner, BehaviorId behavior, std::string scriptSource)
        : runner(&behaviorRunner), owner(behavior), source(std::move(scriptSource)) {
    }

    void run(World& world, FrameNumber frame, IntentTime now) override {
        (void)frame;

        if (hasRun)
            return;

        executionResult = runner->execute(world, owner, now, source);
        hasRun = true;
    }

    const LuaExecutionResult& result() const {
        return executionResult;
    }
};

class TrackingSystem : public System {
public:
    static constexpr std::string_view stableName = "apps.SimulationCli.cpp.TrackingSystem";
    static constexpr std::uint32_t version = 1;
private:
    std::size_t completedRuns = 0;

public:
    void run(World& world, FrameNumber frame, IntentTime now) override {
        (void)world;
        (void)frame;
        (void)now;
        completedRuns++;
    }

    std::size_t runs() const {
        return completedRuns;
    }
};

std::string status_name(LuaExecutionStatus status) {
    switch (status) {
    case LuaExecutionStatus::Success:
        return "success";
    case LuaExecutionStatus::InvalidBehavior:
        return "invalid_behavior";
    case LuaExecutionStatus::SourceLimitExceeded:
        return "source_limit_exceeded";
    case LuaExecutionStatus::SyntaxError:
        return "syntax_error";
    case LuaExecutionStatus::RuntimeError:
        return "runtime_error";
    case LuaExecutionStatus::InstructionLimitExceeded:
        return "instruction_limit_exceeded";
    case LuaExecutionStatus::MemoryLimitExceeded:
        return "memory_limit_exceeded";
    case LuaExecutionStatus::IntentLimitExceeded:
        return "intent_limit_exceeded";
    case LuaExecutionStatus::InvalidProposal:
        return "invalid_proposal";
    case LuaExecutionStatus::CommitFailed:
        return "commit_failed";
    case LuaExecutionStatus::HostError:
        return "host_error";
    }

    return "unknown";
}

std::string priority_name(IntentPriority priority) {
    switch (priority) {
    case IntentPriority::Low:
        return "low";
    case IntentPriority::Medium:
        return "medium";
    case IntentPriority::High:
        return "high";
    }

    return "unknown";
}

template <typename Values>
std::string join_values(const Values& values) {
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

std::string join_phases(const std::vector<std::string>& phases) {
    return join_values(phases);
}

void write_usage_error(std::ostream& error, const std::string& diagnostic) {
    error << "error: " << diagnostic << '\n';
    error << "Try 'liquid_sim_cli --help' for usage.\n";
}

void write_script_result(std::ostream& output, const LuaExecutionResult& result) {
    output << "script status=" << status_name(result.status)
           << " created_intents=" << result.createdIntents.size()
           << " intent_ids=" << join_ids(result.createdIntents)
           << " diagnostic=\"" << escape_cli_text(result.diagnostic) << "\"\n";
}

void write_frame(std::ostream& output, const FrameLog& frame) {
    output << "frame number=" << frame.frame
           << " now_ms=" << frame.now
           << " completed=" << (frame.completed ? "true" : "false")
           << " phases=" << join_phases(frame.phases)
           << " expired_intents=" << frame.expired_intents
           << " resolution_requests=" << frame.resolution_requests
           << " selected_intents=" << frame.selected_intents
           << " systems_completed=" << frame.systems_run
           << '\n';
}

void write_selections(
    std::ostream& output,
    const FrameLog& frame,
    World& world,
    ComponentType<Light> lightType
) {
    for (const auto& [type, selections] : frame.intent_selections) {
        if (type != lightType.id)
            throw std::runtime_error("unexpected selected component type");

        for (const auto& [name, id] : selections) {
            const auto& selected = world.typed_intent(lightType, id);
            output << "selection frame=" << frame.frame
                   << " type=" << LightTypeName
                   << " type_id=" << type
                   << " component=" << escape_cli_text(name)
                   << " intent_id=" << id
                   << " brightness=" << selected.value.brightness
                   << " priority=" << priority_name(selected.priority);

            if (selected.lifetime.kind == IntentLifetimeKind::Persistent) {
                output << " lifetime=persistent";
            } else if (selected.lifetime.kind == IntentLifetimeKind::UntilTime) {
                output << " lifetime=until_time"
                       << " expires_at_ms=" << selected.lifetime.expiresAt;
            } else {
                throw std::runtime_error("unexpected selected intent lifetime");
            }

            output << '\n';
        }
    }
}

int execute_scenario(const Options& options, const std::string& source, std::ostream& output) {
    LuaBehaviorRunner runner;
    Runtime runtime;
    World& world = runtime.world();
    ComponentType<Light> lightType = world.register_component<Light>(LightTypeName);
    world.add_component(lightType, LightComponentName, Light{options.initialBrightness});

    BehaviorId behavior = world.create_behavior();
    world.grant_component_access(
        lightType,
        behavior,
        LightComponentName,
        ComponentAccessMode::ReadWrite
    );
    ComponentSlotId slot = world.get_components(lightType, behavior).at(LightComponentName);

    runner.expose_component(lightType, LightTypeName, light_codec());
    world.register_system<LuaScenarioSystem>(Signature{}, runner, behavior, source);
    world.register_system<TrackingSystem>(Signature{});

    output << "scenario initial_brightness=" << options.initialBrightness
           << " frame_count=" << options.frameTimes.size() << '\n';

    int exitStatus = static_cast<int>(ExitCode::Success);
    bool scriptReported = false;
    std::map<ComponentTypeId, std::map<ComponentName, ComponentSlotId>> resolutions{
        {lightType.id, {{LightComponentName, slot}}}
    };

    for (IntentTime frameTime : options.frameTimes) {
        FrameLog frame = runtime.run_frame(frameTime, resolutions);
        const LuaExecutionResult& scriptResult = world.get_system<LuaScenarioSystem>().result();

        if (!scriptReported) {
            write_script_result(output, scriptResult);
            scriptReported = true;

            if (scriptResult.status == LuaExecutionStatus::HostError) {
                exitStatus = static_cast<int>(ExitCode::HostError);
            } else if (!scriptResult.succeeded()) {
                exitStatus = static_cast<int>(ExitCode::ScriptError);
            }
        }

        write_frame(output, frame);
        write_selections(output, frame, world, lightType);
    }

    const Light* finalLight = world.get_component_named(lightType, LightComponentName);
    if (!finalLight)
        throw std::runtime_error("final light state is unavailable");

    output << "final component=" << LightTypeName << '.' << LightComponentName
           << " brightness=" << finalLight->brightness
           << " tracking_system_runs=" << world.get_system<TrackingSystem>().runs()
           << " frames_completed=" << runtime.frame()
           << " faulted=" << (runtime.faulted() ? "true" : "false")
           << '\n';

    return exitStatus;
}

}

int run_cli(
    const std::vector<std::string>& arguments,
    std::ostream& output,
    std::ostream& error
) {
    ParseResult parsed = parse_arguments(arguments);
    if (!parsed.succeeded) {
        write_usage_error(error, parsed.diagnostic);
        return static_cast<int>(ExitCode::UsageError);
    }

    if (parsed.helpRequested) {
        output << Usage;
        if (!output) {
            error << "error: host failure: \"could not write simulation output\"\n";
            return static_cast<int>(ExitCode::HostError);
        }
        return static_cast<int>(ExitCode::Success);
    }

    try {
        LuaExecutionLimits limits;
        std::string source;
        std::string diagnostic;
        if (!read_script(parsed.options.scriptPath, limits.maxSourceBytes, source, diagnostic)) {
            write_usage_error(error, diagnostic);
            return static_cast<int>(ExitCode::UsageError);
        }

        int status = execute_scenario(parsed.options, source, output);
        if (!output) {
            error << "error: host failure: \"could not write simulation output\"\n";
            return static_cast<int>(ExitCode::HostError);
        }

        return status;
    } catch (const std::exception& exception) {
        error << "error: host failure: \"" << escape_cli_text(exception.what()) << "\"\n";
        return static_cast<int>(ExitCode::HostError);
    } catch (...) {
        error << "error: host failure: \"unknown exception\"\n";
        return static_cast<int>(ExitCode::HostError);
    }
}

}
