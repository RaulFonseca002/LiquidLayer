#pragma once

#include "liquid/Ids.hpp"
#include "liquid/IntentLifetime.hpp"
#include "liquid/world/World.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
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

    bool succeeded() const;
};

class LuaBehaviorRunner {
private:
    struct PendingIntent {
        virtual ~PendingIntent() = default;
        virtual IntentId commit(World& world, BehaviorId owner) = 0;
    };

    template <typename Component>
    struct TypedPendingIntent : PendingIntent {
        ComponentType<Component> type;
        ComponentName name;
        IntentLifetime lifetime;
        IntentPriority priority;
        Component value;

        TypedPendingIntent(
            ComponentType<Component> componentType,
            ComponentName componentName,
            IntentLifetime intentLifetime,
            IntentPriority intentPriority,
            Component intentValue
        )
            : type(componentType),
              name(std::move(componentName)),
              lifetime(intentLifetime),
              priority(intentPriority),
              value(std::move(intentValue))
        {
        }

        IntentId commit(World& world, BehaviorId owner) override {
            std::map<ComponentName, ComponentSlotId> components = world.get_components(type, owner);
            auto target = components.find(name);

            if (target == components.end() || !world.can_write_component(type, owner, name))
                throw std::runtime_error("component write access denied");

            return world.create_intent(
                owner,
                type,
                target->second,
                lifetime,
                std::move(value),
                priority
            );
        }
    };

    struct CapabilityDescription {
        std::size_t binding = 0;
        ComponentName name;
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
            IntentPriority
        )> makePending;
    };

    struct Impl;
    std::unique_ptr<Impl> impl;

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

    LuaExecutionResult execute(
        World& world,
        BehaviorId owner,
        IntentTime now,
        std::string_view source
    );
};

template <typename Component>
void LuaBehaviorRunner::expose_component(
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
                descriptions.push_back({bindingIndex, name, readable, writable});
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
        IntentPriority priority
    ) -> std::unique_ptr<PendingIntent> {
        return std::make_unique<TypedPendingIntent<Component>>(
            type,
            name,
            lifetime,
            priority,
            decode(value)
        );
    };

    register_binding(std::move(binding));
}

}
