#pragma once

#include "liquid/Ids.hpp"
#include "liquid/IntentLifetime.hpp"
#include "liquid/scripting/LuaValueSchema.hpp"
#include "liquid/world/World.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace liquid::scripting {

class LuaValue {
public:
    using Array = std::vector<LuaValue>;
    using Table = std::map<std::string, LuaValue>;
    using Storage = std::variant<bool, std::int64_t, double, std::string, Array, Table>;

private:
    Storage storedValue;

public:
    LuaValue(bool value);
    LuaValue(std::int64_t value);
    LuaValue(int value);
    LuaValue(double value);
    LuaValue(std::string value);
    LuaValue(const char* value);
    LuaValue(Array value);
    LuaValue(Table value);

    const Storage& storage() const;
    bool as_bool() const;
    std::int64_t as_integer() const;
    double as_number() const;
    const std::string& as_string() const;
    const Array& as_array() const;
    const Table& as_table() const;

    friend bool operator==(const LuaValue&, const LuaValue&) = default;
};

template <typename Component>
struct LuaComponentCodec {
    std::function<LuaValue(const Component&)> encode;
    std::function<Component(const LuaValue&)> decode;
};

struct LuaExecutionLimits {
    std::size_t maxSourceBytes = 64 * 1024;
    std::size_t maxMemoryBytes = 8 * 1024 * 1024;
    std::size_t maxInstructions = 100'000;
    std::size_t maxDiagnosticBytes = 4 * 1024;
    std::size_t maxCreatedIntents = 64;
    std::size_t maxCancelledIntents = 64;
    std::size_t maxWatches = 64;
    std::size_t maxTableDepth = 16;
    std::size_t maxTableEntries = 4'096;
    std::size_t maxStringBytes = 64 * 1024;
    std::size_t maxBufferedValueBytes = 8 * 1024 * 1024;
    bool recordFullSource = true;
};

enum class LuaExecutionStatus {
    Success,
    InvalidBehavior,
    SourceLimitExceeded,
    SyntaxError,
    RuntimeError,
    InstructionLimitExceeded,
    MemoryLimitExceeded,
    IntentLimitExceeded,
    InvalidProposal,
    CommitFailed,
    HostError
};

struct LuaExecutionResult {
    LuaExecutionStatus status = LuaExecutionStatus::Success;
    std::string diagnostic;
    std::vector<IntentId> createdIntents;
    std::vector<IntentId> cancelledIntents;

    struct Watch {
        TypeName type;
        ComponentName name;

        friend bool operator==(const Watch&, const Watch&) = default;
    };

    std::vector<Watch> watches;

    bool succeeded() const;
};

// Complete in LuaCapabilityManifest.hpp; callers of capability_manifest include it.
class LuaManifestResult;
struct LuaScopeGrant;
struct LuaScopeTarget;

class LuaBehaviorRunner {
private:
    struct PendingIntent {
        virtual ~PendingIntent() = default;
        virtual const IntentName& intent_name() const = 0;
        virtual void validate(World& world, BehaviorId owner) const = 0;
        virtual IntentId commit(
            World& world,
            BehaviorId owner,
            detail::IntentRegistry::Transaction& transaction) = 0;
    };

    template <typename Component>
    struct TypedPendingIntent : PendingIntent {
        ComponentType<Component> type;
        ComponentName name;
        IntentLifetime lifetime;
        IntentPriority priority;
        IntentName intentName;
        Component value;

        TypedPendingIntent(
            ComponentType<Component> componentType,
            ComponentName componentName,
            IntentLifetime intentLifetime,
            IntentPriority intentPriority,
            IntentName stableIntentName,
            Component intentValue
        )
            : type(componentType),
              name(std::move(componentName)),
              lifetime(intentLifetime),
              priority(intentPriority),
              intentName(std::move(stableIntentName)),
              value(std::move(intentValue))
        {
        }

        const IntentName& intent_name() const override {
            return intentName;
        }

        void validate(World& world, BehaviorId owner) const override {
            std::map<ComponentName, ComponentSlotId> components = world.get_components(type, owner);
            auto target = components.find(name);
            if (target == components.end() || !world.can_write_component(type, owner, name))
                throw std::runtime_error("component write access denied");
        }

        IntentId commit(
            World& world,
            BehaviorId owner,
            detail::IntentRegistry::Transaction& transaction
        ) override {
            std::map<ComponentName, ComponentSlotId> components = world.get_components(type, owner);
            auto target = components.find(name);

            if (target == components.end() || !world.can_write_component(type, owner, name))
                throw std::runtime_error("component write access denied");

            return world.create_intent_transaction(
                transaction,
                owner,
                type,
                target->second,
                lifetime,
                std::move(value),
                priority,
                std::move(intentName)
            );
        }
    };

    struct CapabilityDescription {
        std::size_t binding = 0;
        ComponentName name;
        ComponentSlotId slot{};
        bool readable = false;
        bool writable = false;
    };

    struct Binding {
        TypeName scriptName;
        ComponentTypeId type = InvalidComponentTypeId;
        std::function<std::vector<CapabilityDescription>(World&, BehaviorId, std::size_t)> describe;
        std::function<LuaValue(World&, BehaviorId, const ComponentName&)> snapshot;
        std::function<std::unique_ptr<PendingIntent>(
            const ComponentName&,
            const LuaValue&,
            IntentLifetime,
            IntentPriority,
            IntentName
        )> makePending;
        // Trusted-host scope capture: no BehaviorId, no access grant consulted.
        std::function<std::optional<ComponentSlotId>(World&, const ComponentName&)> resolveTarget;
        std::function<LuaValue(World&, ComponentSlotId)> encodeTarget;
        // Present only for bindings registered through the metadata overload.
        std::optional<LuaModelBindingMetadata> metadata;
    };

    struct Impl;
    std::unique_ptr<Impl> impl;

    template <typename Component>
    static Binding make_binding(
        ComponentType<Component> type,
        TypeName scriptName,
        LuaComponentCodec<Component> codec
    );

    void register_binding(Binding binding);

public:
    explicit LuaBehaviorRunner(LuaExecutionLimits limits = {});
    ~LuaBehaviorRunner();

    LuaBehaviorRunner(const LuaBehaviorRunner&) = delete;
    LuaBehaviorRunner& operator=(const LuaBehaviorRunner&) = delete;
    LuaBehaviorRunner(LuaBehaviorRunner&&) = delete;
    LuaBehaviorRunner& operator=(LuaBehaviorRunner&&) = delete;

    template <typename Component>
    void expose_component(
        ComponentType<Component> type,
        TypeName scriptName,
        LuaComponentCodec<Component> codec
    );

    // Described binding: validates the metadata against the L0 ceilings and
    // this runner's effective limits before registering one binding.
    template <typename Component>
    void expose_component(
        ComponentType<Component> type,
        TypeName scriptName,
        LuaComponentCodec<Component> codec,
        LuaModelBindingMetadata metadata
    );

    // Copied manifest of the described capabilities `behavior` currently holds.
    // The first successful capture freezes binding registration.
    LuaManifestResult capability_manifest(World& world, BehaviorId behavior, IntentTime now);

    // Host-only prospective manifest for trusted grants; creates no behavior.
    // Write-only grants are not read. The first successful capture freezes
    // binding registration; a failed capture does not.
    LuaManifestResult scope_manifest(
        World& world,
        std::span<const LuaScopeGrant> grants,
        IntentTime now
    );

    // Host-only target identities of trusted grants, in grant order, under the
    // same grant admission as scope_manifest. Reads no value and does not
    // freeze registration. Empty when any grant is not admitted; host
    // exceptions propagate.
    std::optional<std::vector<LuaScopeTarget>> scope_targets(
        World& world,
        std::span<const LuaScopeGrant> grants
    ) const;

    LuaExecutionResult execute(
        World& world,
        BehaviorId owner,
        IntentTime now,
        std::string_view source
    );

    struct ComponentChange {
        TypeName type;
        ComponentName name;
        LuaValue before;
        LuaValue after;
    };

    LuaExecutionResult execute_lifecycle(
        World& world,
        BehaviorId owner,
        FrameNumber frame,
        IntentTime now,
        IntentTime delta,
        bool start,
        const std::vector<ComponentChange>& changes,
        std::string_view source
    );

    std::optional<LuaValue> snapshot(
        World& world,
        BehaviorId owner,
        const LuaExecutionResult::Watch& watch
    );

    // Diagnostic view of the bounded capability-description cache.
    std::size_t cached_capability_entries() const;
};

template <typename Component>
void LuaBehaviorRunner::expose_component(
    ComponentType<Component> type,
    TypeName scriptName,
    LuaComponentCodec<Component> codec
) {
    register_binding(make_binding(type, std::move(scriptName), std::move(codec)));
}

template <typename Component>
void LuaBehaviorRunner::expose_component(
    ComponentType<Component> type,
    TypeName scriptName,
    LuaComponentCodec<Component> codec,
    LuaModelBindingMetadata metadata
) {
    Binding binding = make_binding(type, std::move(scriptName), std::move(codec));
    binding.metadata = std::move(metadata);
    register_binding(std::move(binding));
}

template <typename Component>
LuaBehaviorRunner::Binding LuaBehaviorRunner::make_binding(
    ComponentType<Component> type,
    TypeName scriptName,
    LuaComponentCodec<Component> codec
) {
    if (!codec.encode || !codec.decode)
        throw std::invalid_argument("Lua component codec requires encode and decode functions");

    Binding binding;
    binding.scriptName = std::move(scriptName);
    binding.type = type.id;
    binding.describe = [type](World& world, BehaviorId owner, std::size_t bindingIndex) {
        std::vector<CapabilityDescription> descriptions;

        for (const auto& [name, slot] : world.get_components(type, owner)) {
            (void)slot;
            bool readable = world.can_read_component(type, owner, name);
            bool writable = world.can_write_component(type, owner, name);

            if (readable || writable)
                descriptions.push_back({bindingIndex, name, slot, readable, writable});
        }

        return descriptions;
    };
    binding.snapshot = [type, encode = codec.encode](World& world, BehaviorId owner, const ComponentName& name) {
        const Component* component = world.read_component(type, owner, name);

        if (!component)
            throw std::runtime_error("component snapshot is unavailable");

        return encode(*component);
    };
    binding.makePending = [type, decode = codec.decode](
        const ComponentName& name,
        const LuaValue& value,
        IntentLifetime lifetime,
        IntentPriority priority,
        IntentName intentName
    ) -> std::unique_ptr<PendingIntent> {
        return std::make_unique<TypedPendingIntent<Component>>(
            type,
            name,
            lifetime,
            priority,
            std::move(intentName),
            decode(value)
        );
    };
    binding.resolveTarget = [type](World& world, const ComponentName& name) -> std::optional<ComponentSlotId> {
        if (!world.has_component_named(type, name))
            return std::nullopt;

        return world.component_target(type, name).slot;
    };
    binding.encodeTarget = [type, encode = codec.encode](World& world, ComponentSlotId slot) {
        const Component* component = world.resolve_component(type, slot);

        if (!component)
            throw std::runtime_error("component snapshot is unavailable");

        return encode(*component);
    };

    return binding;
}

}
