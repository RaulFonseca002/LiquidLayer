#pragma once

#include "liquid/Ids.hpp"
#include "liquid/scripting/LuaBehaviorRunner.hpp"
#include "liquid/scripting/LuaValueSchema.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace liquid::scripting {

inline constexpr std::string_view LuaAuthoringContract = "liquid.lua.authoring/1";
inline constexpr std::size_t LuaManifestMaxCapabilities = 128;
inline constexpr std::size_t LuaManifestMaxDescriptionBytes = 65'536;
inline constexpr std::size_t LuaManifestMaxLogicalBytes = 1024 * 1024;
inline constexpr std::size_t LuaMaxAccessExpressionBytes = 4'096;
inline constexpr std::size_t LuaMaxManifestDiagnosticBytes = 4'096;

struct LuaManifestCapability {
    TypeName scriptTypeName;
    ComponentName componentName;
    // Exact Lua expression that addresses the capability table, e.g. `access.Light.office`.
    std::string accessExpression;
    ComponentAccessMode mode = ComponentAccessMode::Read;
    std::string description;
    std::optional<LuaValueSchema> readSchema;
    std::optional<LuaValue> readValue;
    std::optional<LuaValueSchema> writeSchema;
};

// One trusted-host selection for a prospective scope. Only host APIs accept grants;
// neither a source string nor a manifest can create one.
struct LuaScopeGrant {
    TypeName scriptTypeName;
    ComponentName componentName;
    ComponentAccessMode mode = ComponentAccessMode::Read;
};

// Resolved identity of one grant target, without its value.
struct LuaScopeTarget {
    ComponentTypeId type = InvalidComponentTypeId;
    ComponentSlotId slot{};

    friend bool operator==(const LuaScopeTarget&, const LuaScopeTarget&) = default;
};

enum class LuaManifestCaptureKind {
    Behavior,
    // No live BehaviorId: behavior() is invalid and access_revision() is 0.
    ProspectiveScope
};

// Host-only capture identity for later staleness checks. Never serialized to an author.
class LuaManifestCapture {
public:
    struct Entry {
        std::size_t bindingIndex = 0;
        ComponentTypeId type = InvalidComponentTypeId;
        ComponentSlotId slot{};
    };

    LuaManifestCaptureKind kind() const;
    WorldInstanceId world_instance() const;
    BehaviorId behavior() const;
    BehaviorAccessRevision access_revision() const;
    std::uint64_t runner_id() const;
    const LuaExecutionLimits& limits() const;
    // Parallel to LuaCapabilityManifest::capabilities().
    const std::vector<Entry>& entries() const;

private:
    LuaManifestCaptureKind captureKind = LuaManifestCaptureKind::Behavior;
    WorldInstanceId worldInstance = 0;
    BehaviorId behaviorId{};
    BehaviorAccessRevision accessRevision = 0;
    std::uint64_t runnerId = 0;
    LuaExecutionLimits effectiveLimits;
    std::vector<Entry> capturedEntries;

    LuaManifestCapture(
        WorldInstanceId world,
        BehaviorId behavior,
        BehaviorAccessRevision revision,
        std::uint64_t runner,
        LuaExecutionLimits limits
    );
    // Prospective-scope capture.
    LuaManifestCapture(WorldInstanceId world, std::uint64_t runner, LuaExecutionLimits limits);

    friend class LuaCapabilityManifest;
    friend class LuaBehaviorRunner;
};

enum class LuaManifestErrorCode {
    InvalidBehavior,
    SnapshotUnavailable,
    SchemaMismatch,
    LimitExceeded,
    HostError,
    // Prospective scope: empty, duplicate, unnamed, unknown, undescribed or missing target.
    InvalidGrant
};

struct LuaManifestError {
    LuaManifestErrorCode code = LuaManifestErrorCode::HostError;
    std::string diagnostic;
};

class LuaManifestResult;

class LuaCapabilityManifest {
public:
    std::string_view authoring_contract() const;
    IntentTime now_ms() const;
    const LuaExecutionLimits& limits() const;
    const std::vector<std::string>& notes() const;
    // Sorted by script type name, then component name, in unsigned-byte order.
    const std::vector<LuaManifestCapability>& capabilities() const;
    const LuaManifestCapture& capture() const;
    // Logical size charged against LuaManifestMaxLogicalBytes when built.
    std::size_t logical_bytes() const;

private:
    class Builder;

    IntentTime nowMs = 0;
    std::vector<LuaManifestCapability> entries;
    LuaManifestCapture captureData;
    std::size_t logicalBytes = 0;

    LuaCapabilityManifest(IntentTime now, LuaManifestCapture capture);

    friend class LuaBehaviorRunner;
};

// Accumulates capabilities for one capture and enforces the manifest
// aggregates, so a caller gets one complete manifest or one error.
class LuaCapabilityManifest::Builder {
public:
    Builder(IntentTime now, LuaManifestCapture capture);

    // `capability.accessExpression` is generated here.
    std::optional<LuaManifestError> add(
        LuaManifestCapability capability,
        LuaManifestCapture::Entry entry
    );
    LuaManifestResult finish();

private:
    IntentTime nowMs;
    LuaManifestCapture captureData;
    std::vector<std::pair<LuaManifestCapability, LuaManifestCapture::Entry>> items;
    std::size_t valueNodes = 0;
    std::size_t descriptionBytes = 0;
    std::size_t logicalBytes = 0;
};

class LuaManifestResult {
public:
    bool ok() const;
    // Throws std::logic_error when the capture failed.
    const LuaCapabilityManifest& manifest() const;
    // Throws std::logic_error when the capture succeeded.
    const LuaManifestError& error() const;

private:
    std::variant<LuaCapabilityManifest, LuaManifestError> outcome;

    explicit LuaManifestResult(LuaCapabilityManifest manifest);
    explicit LuaManifestResult(LuaManifestError error);

    friend class LuaCapabilityManifest;
    friend class LuaBehaviorRunner;
};

}
