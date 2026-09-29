#include "liquid/scripting/LuaCapabilityManifest.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace liquid::scripting {

namespace {

// New L0 diagnostics end in "..." when truncated.
LuaManifestError limit_error(std::string diagnostic) {
    diagnostic = "Lua capability manifest: " + diagnostic;
    if (diagnostic.size() > LuaMaxManifestDiagnosticBytes) {
        diagnostic.resize(LuaMaxManifestDiagnosticBytes - 3);
        diagnostic += "...";
    }
    return LuaManifestError{LuaManifestErrorCode::LimitExceeded, std::move(diagnostic)};
}

LuaManifestError logical_limit_error() {
    return limit_error("manifest exceeds 1 MiB logical bytes");
}

const std::vector<std::string>& authoring_notes() {
    static const std::vector<std::string> notes = {
        "Each run executes in a fresh Lua VM; no state persists between runs.",
        "The host fixes the owner behavior and the time (now_ms); scripts cannot choose either.",
        "Lifecycle callbacks must use named proposals (a stable `name`).",
        "Proposal lifetime is persistent by default (`lifetime = \"persistent\"`) "
        "or lasts until now_ms + `duration_ms` (until-time).",
        "An accepted proposal is not a claim of physical success.",
        "Lua keeps integer and float distinct: 1 is an Integer and 1.0 is a Number.",
        "A literal {} is an Object. Host-encoded arrays keep a private array marker and "
        "round-trip unchanged, but Lua has no constructor for a new empty array."
    };
    return notes;
}

// Contract logical payload accounting (LIQUID_IMPLEMENTATION_CONTRACT.md):
// 8 bytes per numeric scalar, ID or enum, 1 per boolean or optional-presence
// flag, byte length for strings, and 8 per collection element or record
// field plus key bytes for maps. LuaValue has no nil alternative, so nil
// never reaches a manifest; an absent optional costs only its flag. Every
// public manifest field is charged by the constants and functions below, so
// a new field has one place to update.
constexpr std::size_t LogicalScalarBytes = 8;
constexpr std::size_t LogicalFlagBytes = 1;
constexpr std::size_t LogicalElementBytes = 8;

// LuaCapabilityManifest: authoring contract, now_ms, limits, notes, capabilities.
constexpr std::size_t ManifestFields = 5;
// LuaExecutionLimits: eleven size limits and recordFullSource.
constexpr std::size_t LimitSizeFields = 11;
constexpr std::size_t LimitFlagFields = 1;
static_assert(sizeof(LuaExecutionLimits) == (LimitSizeFields + LimitFlagFields) * sizeof(std::size_t),
    "update the manifest logical accounting for the new LuaExecutionLimits field");
// LuaManifestCapability: script type, component, expression, mode,
// description, read schema, read value, write schema; the last three are
// optional.
constexpr std::size_t CapabilityFields = 8;
constexpr std::size_t CapabilityOptionalFields = 3;
// One capabilities element before its strings, schemas and value.
constexpr std::size_t CapabilityFixedBytes = LogicalElementBytes
    + CapabilityFields * LogicalElementBytes
    + LogicalScalarBytes
    + CapabilityOptionalFields * LogicalFlagBytes;

std::size_t manifest_fixed_bytes() {
    std::size_t bytes = ManifestFields * LogicalElementBytes
        + LuaAuthoringContract.size()
        + LogicalScalarBytes
        + (LimitSizeFields + LimitFlagFields) * LogicalElementBytes
        + LimitSizeFields * LogicalScalarBytes
        + LimitFlagFields * LogicalFlagBytes;
    for (const std::string& note : authoring_notes())
        bytes += LogicalElementBytes + note.size();
    return bytes;
}

// Running logical total that never passes the 1 MiB manifest ceiling.
class LogicalBudget {
public:
    explicit LogicalBudget(std::size_t used)
        : used(used)
    {
    }

    bool add(std::size_t bytes) {
        if (bytes > LuaManifestMaxLogicalBytes - used)
            return false;
        used += bytes;
        return true;
    }

    std::size_t total() const {
        return used;
    }

private:
    std::size_t used;
};

// Adds the value's nodes to `nodes` and its logical payload to `budget`.
std::optional<LuaManifestError> count_value(const LuaValue& value, std::size_t& nodes, LogicalBudget& budget) {
    if (++nodes > LuaMaxValueNodes)
        return limit_error("manifest exceeds 16384 copied value nodes");

    const LuaValue::Storage& storage = value.storage();
    if (const LuaValue::Array* items = std::get_if<LuaValue::Array>(&storage)) {
        for (const LuaValue& item : *items) {
            if (!budget.add(LogicalElementBytes))
                return logical_limit_error();
            if (std::optional<LuaManifestError> error = count_value(item, nodes, budget))
                return error;
        }
        return std::nullopt;
    }
    if (const LuaValue::Table* table = std::get_if<LuaValue::Table>(&storage)) {
        for (const auto& [key, field] : *table) {
            if (!budget.add(LogicalElementBytes) || !budget.add(key.size()))
                return logical_limit_error();
            if (std::optional<LuaManifestError> error = count_value(field, nodes, budget))
                return error;
        }
        return std::nullopt;
    }

    std::size_t bytes = LogicalScalarBytes;
    if (std::holds_alternative<bool>(storage))
        bytes = LogicalFlagBytes;
    else if (const std::string* text = std::get_if<std::string>(&storage))
        bytes = text->size();
    if (!budget.add(bytes))
        return logical_limit_error();
    return std::nullopt;
}

}

LuaManifestCapture::LuaManifestCapture(
    WorldInstanceId world,
    BehaviorId behavior,
    BehaviorAccessRevision revision,
    std::uint64_t runner,
    LuaExecutionLimits limits
)
    : worldInstance(world),
      behaviorId(behavior),
      accessRevision(revision),
      runnerId(runner),
      effectiveLimits(limits)
{
}

WorldInstanceId LuaManifestCapture::world_instance() const {
    return worldInstance;
}

BehaviorId LuaManifestCapture::behavior() const {
    return behaviorId;
}

BehaviorAccessRevision LuaManifestCapture::access_revision() const {
    return accessRevision;
}

std::uint64_t LuaManifestCapture::runner_id() const {
    return runnerId;
}

const LuaExecutionLimits& LuaManifestCapture::limits() const {
    return effectiveLimits;
}

const std::vector<LuaManifestCapture::Entry>& LuaManifestCapture::entries() const {
    return capturedEntries;
}

LuaCapabilityManifest::LuaCapabilityManifest(IntentTime now, LuaManifestCapture capture)
    : nowMs(now),
      captureData(std::move(capture))
{
}

std::string_view LuaCapabilityManifest::authoring_contract() const {
    return LuaAuthoringContract;
}

IntentTime LuaCapabilityManifest::now_ms() const {
    return nowMs;
}

const LuaExecutionLimits& LuaCapabilityManifest::limits() const {
    return captureData.limits();
}

const std::vector<std::string>& LuaCapabilityManifest::notes() const {
    return authoring_notes();
}

const std::vector<LuaManifestCapability>& LuaCapabilityManifest::capabilities() const {
    return entries;
}

const LuaManifestCapture& LuaCapabilityManifest::capture() const {
    return captureData;
}

LuaCapabilityManifest::Builder::Builder(IntentTime now, LuaManifestCapture capture)
    : nowMs(now),
      captureData(std::move(capture)),
      logicalBytes(manifest_fixed_bytes())
{
}

std::optional<LuaManifestError> LuaCapabilityManifest::Builder::add(
    LuaManifestCapability capability,
    LuaManifestCapture::Entry entry
) {
    if (capability.scriptTypeName.size() > LuaMaxNameBytes)
        return limit_error("script type name exceeds 256 bytes");
    if (capability.componentName.size() > LuaMaxNameBytes)
        return limit_error("component name exceeds 256 bytes");
    if (items.size() >= LuaManifestMaxCapabilities)
        return limit_error("manifest exceeds 128 capabilities");

    std::string expression = "access"
        + LuaValueSchema::lua_index_segment(capability.scriptTypeName)
        + LuaValueSchema::lua_index_segment(capability.componentName);
    if (expression.size() > LuaMaxAccessExpressionBytes)
        return limit_error("access expression exceeds 4096 bytes");

    std::size_t nodes = valueNodes;
    LogicalBudget budget(logicalBytes);
    if (capability.readValue) {
        if (std::optional<LuaManifestError> error = count_value(*capability.readValue, nodes, budget))
            return error;
    }

    std::size_t descriptions = capability.description.size();
    std::size_t readSchemaBytes = 0;
    std::size_t writeSchemaBytes = 0;
    if (capability.readSchema) {
        descriptions += capability.readSchema->description_bytes();
        readSchemaBytes = capability.readSchema->logical_bytes();
    }
    if (capability.writeSchema) {
        descriptions += capability.writeSchema->description_bytes();
        writeSchemaBytes = capability.writeSchema->logical_bytes();
    }
    if (descriptions > LuaManifestMaxDescriptionBytes - std::min(descriptionBytes, LuaManifestMaxDescriptionBytes))
        return limit_error("manifest descriptions exceed 65536 bytes");

    // Schema logical bytes already include their descriptions; duplicated
    // read and write schemas are each counted.
    for (std::size_t part : {
        CapabilityFixedBytes,
        capability.scriptTypeName.size(),
        capability.componentName.size(),
        expression.size(),
        capability.description.size(),
        readSchemaBytes,
        writeSchemaBytes
    }) {
        if (!budget.add(part))
            return logical_limit_error();
    }

    capability.accessExpression = std::move(expression);
    valueNodes = nodes;
    descriptionBytes += descriptions;
    logicalBytes = budget.total();
    items.emplace_back(std::move(capability), entry);
    return std::nullopt;
}

LuaManifestResult LuaCapabilityManifest::Builder::finish() {
    std::stable_sort(items.begin(), items.end(), [](const auto& left, const auto& right) {
        if (left.first.scriptTypeName != right.first.scriptTypeName)
            return left.first.scriptTypeName < right.first.scriptTypeName;
        return left.first.componentName < right.first.componentName;
    });

    LuaCapabilityManifest manifest(nowMs, captureData);
    manifest.entries.reserve(items.size());
    manifest.captureData.capturedEntries.reserve(items.size());
    for (auto& [capability, entry] : items) {
        manifest.entries.push_back(std::move(capability));
        manifest.captureData.capturedEntries.push_back(entry);
    }
    items.clear();
    return LuaManifestResult(std::move(manifest));
}

LuaManifestResult::LuaManifestResult(LuaCapabilityManifest manifest)
    : outcome(std::move(manifest))
{
}

LuaManifestResult::LuaManifestResult(LuaManifestError error)
    : outcome(std::move(error))
{
}

bool LuaManifestResult::ok() const {
    return std::holds_alternative<LuaCapabilityManifest>(outcome);
}

const LuaCapabilityManifest& LuaManifestResult::manifest() const {
    if (const LuaCapabilityManifest* manifest = std::get_if<LuaCapabilityManifest>(&outcome))
        return *manifest;
    throw std::logic_error("Lua capability manifest capture failed");
}

const LuaManifestError& LuaManifestResult::error() const {
    if (const LuaManifestError* error = std::get_if<LuaManifestError>(&outcome))
        return *error;
    throw std::logic_error("Lua capability manifest capture succeeded");
}

}
