#include "liquid/scripting/LuaBehaviorRunner.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace liquid::scripting {

LuaValue::LuaValue(bool value)
    : storedValue(value)
{
}

LuaValue::LuaValue(std::int64_t value)
    : storedValue(value)
{
}

LuaValue::LuaValue(int value)
    : storedValue(static_cast<std::int64_t>(value))
{
}

LuaValue::LuaValue(double value)
    : storedValue(value)
{
}

LuaValue::LuaValue(std::string value)
    : storedValue(std::move(value))
{
}

LuaValue::LuaValue(const char* value)
    : storedValue(std::string(value))
{
}

LuaValue::LuaValue(Array value)
    : storedValue(std::move(value))
{
}

LuaValue::LuaValue(Table value)
    : storedValue(std::move(value))
{
}

const LuaValue::Storage& LuaValue::storage() const {
    return storedValue;
}

bool LuaValue::as_bool() const {
    if (const bool* value = std::get_if<bool>(&storedValue))
        return *value;

    throw std::runtime_error("Lua value is not a boolean");
}

std::int64_t LuaValue::as_integer() const {
    if (const std::int64_t* value = std::get_if<std::int64_t>(&storedValue))
        return *value;

    throw std::runtime_error("Lua value is not an integer");
}

double LuaValue::as_number() const {
    if (const double* value = std::get_if<double>(&storedValue))
        return *value;

    throw std::runtime_error("Lua value is not a number");
}

const std::string& LuaValue::as_string() const {
    if (const std::string* value = std::get_if<std::string>(&storedValue))
        return *value;

    throw std::runtime_error("Lua value is not a string");
}

const LuaValue::Array& LuaValue::as_array() const {
    if (const Array* value = std::get_if<Array>(&storedValue))
        return *value;

    throw std::runtime_error("Lua value is not an array");
}

const LuaValue::Table& LuaValue::as_table() const {
    if (const Table* value = std::get_if<Table>(&storedValue))
        return *value;

    throw std::runtime_error("Lua value is not a table");
}

bool LuaExecutionResult::succeeded() const {
    return status == LuaExecutionStatus::Success;
}

namespace {

constexpr int InstructionHookStep = 100;
char EnvironmentRegistryKey;
char ArrayMetatableRegistryKey;

struct LuaMemoryBudget {
    std::size_t current = 0;
    std::size_t limit = 0;
    bool exceeded = false;
};

void* budget_allocator(void* userData, void* pointer, std::size_t oldSize, std::size_t newSize) noexcept {
    auto* budget = static_cast<LuaMemoryBudget*>(userData);

    if (newSize == 0) {
        if (pointer) {
            budget->current = oldSize <= budget->current ? budget->current - oldSize : 0;
            std::free(pointer);
        }

        return nullptr;
    }

    std::size_t accountedOldSize = pointer ? oldSize : 0;
    std::size_t base = accountedOldSize <= budget->current ? budget->current - accountedOldSize : 0;

    if (newSize > budget->limit || base > budget->limit - newSize) {
        budget->exceeded = true;
        return nullptr;
    }

    void* resized = std::realloc(pointer, newSize);

    if (!resized) {
        budget->exceeded = true;
        return nullptr;
    }

    budget->current = base + newSize;
    return resized;
}

std::string bounded_diagnostic(std::string_view diagnostic, std::size_t limit) {
    if (diagnostic.size() <= limit)
        return std::string(diagnostic);

    return std::string(diagnostic.substr(0, limit));
}

class ExecutionFailure : public std::runtime_error {
public:
    LuaExecutionStatus status;

    ExecutionFailure(LuaExecutionStatus failureStatus, const std::string& message)
        : std::runtime_error(message), status(failureStatus)
    {
    }
};

void raw_get_string(lua_State* state, int tableIndex, std::string_view key) {
    tableIndex = lua_absindex(state, tableIndex);
    lua_pushlstring(state, key.data(), key.size());
    lua_rawget(state, tableIndex);
}

void raw_set_string(lua_State* state, int tableIndex, std::string_view key) {
    tableIndex = lua_absindex(state, tableIndex);
    lua_pushlstring(state, key.data(), key.size());
    lua_insert(state, -2);
    lua_rawset(state, tableIndex);
}

void copy_raw_field(lua_State* state, int sourceIndex, int destinationIndex, const char* name) {
    sourceIndex = lua_absindex(state, sourceIndex);
    destinationIndex = lua_absindex(state, destinationIndex);
    lua_pushstring(state, name);
    lua_rawget(state, sourceIndex);
    lua_pushstring(state, name);
    lua_insert(state, -2);
    lua_rawset(state, destinationIndex);
}

void remove_raw_field(lua_State* state, int tableIndex, const char* name) {
    tableIndex = lua_absindex(state, tableIndex);
    lua_pushstring(state, name);
    lua_pushnil(state);
    lua_rawset(state, tableIndex);
}

struct ValueReadState {
    std::size_t entries = 0;
    std::size_t maxEntries = 0;
    std::size_t maxDepth = 0;
    std::size_t maxStringBytes = 0;
    std::size_t* bufferedBytes = nullptr;
    std::size_t maxBufferedBytes = 0;
    std::set<const void*> seenTables;
};

void consume_buffered_bytes(
    std::size_t& bufferedBytes,
    std::size_t amount,
    std::size_t limit,
    LuaExecutionStatus status,
    const char* diagnostic
) {
    if (amount > limit || bufferedBytes > limit - amount)
        throw ExecutionFailure(status, diagnostic);

    bufferedBytes += amount;
}

LuaValue read_lua_value(lua_State* state, int index, ValueReadState& readState, std::size_t depth);

LuaValue read_lua_table(lua_State* state, int index, ValueReadState& readState, std::size_t depth) {
    if (depth > readState.maxDepth)
        throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "Lua value exceeds table depth limit");

    consume_buffered_bytes(
        *readState.bufferedBytes,
        sizeof(LuaValue),
        readState.maxBufferedBytes,
        LuaExecutionStatus::InvalidProposal,
        "Lua values exceed buffered data limit"
    );

    index = lua_absindex(state, index);
    const void* identity = lua_topointer(state, index);
    bool isHostArray = false;

    if (lua_getmetatable(state, index)) {
        lua_rawgetp(state, -1, &ArrayMetatableRegistryKey);
        isHostArray = lua_toboolean(state, -1) != 0;
        lua_pop(state, 2);
    }

    if (!readState.seenTables.insert(identity).second)
        throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "Lua value contains a repeated or cyclic table");

    LuaValue::Table object;
    std::map<std::size_t, LuaValue> arrayValues;
    bool hasStringKeys = false;
    bool hasIntegerKeys = isHostArray;

    lua_pushnil(state);
    while (lua_next(state, index) != 0) {
        readState.entries++;
        if (readState.entries > readState.maxEntries) {
            lua_pop(state, 2);
            throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "Lua value exceeds table entry limit");
        }

        int keyType = lua_type(state, -2);
        LuaValue value = read_lua_value(state, -1, readState, depth + 1);

        if (keyType == LUA_TSTRING) {
            if (hasIntegerKeys) {
                lua_pop(state, 2);
                throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "Lua value cannot mix array and object keys");
            }

            std::size_t length = 0;
            const char* key = lua_tolstring(state, -2, &length);
            if (length > readState.maxStringBytes) {
                lua_pop(state, 2);
                throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "Lua table key exceeds string size limit");
            }
            consume_buffered_bytes(
                *readState.bufferedBytes,
                length,
                readState.maxBufferedBytes,
                LuaExecutionStatus::InvalidProposal,
                "Lua values exceed buffered data limit"
            );
            object.emplace(std::string(key, length), std::move(value));
            hasStringKeys = true;
        } else if (keyType == LUA_TNUMBER && lua_isinteger(state, -2)) {
            if (hasStringKeys) {
                lua_pop(state, 2);
                throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "Lua value cannot mix array and object keys");
            }

            lua_Integer rawKey = lua_tointeger(state, -2);
            if (rawKey <= 0) {
                lua_pop(state, 2);
                throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "Lua array keys must be positive integers");
            }

            arrayValues.emplace(static_cast<std::size_t>(rawKey), std::move(value));
            hasIntegerKeys = true;
        } else {
            lua_pop(state, 2);
            throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "Lua value contains an unsupported table key");
        }

        lua_pop(state, 1);
    }

    readState.seenTables.erase(identity);

    if (!hasIntegerKeys)
        return LuaValue(std::move(object));

    LuaValue::Array array;
    array.reserve(arrayValues.size());

    std::size_t expected = 1;
    for (auto& [position, value] : arrayValues) {
        if (position != expected)
            throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "Lua arrays must be contiguous and start at one");

        array.push_back(std::move(value));
        expected++;
    }

    return LuaValue(std::move(array));
}

LuaValue read_lua_value(lua_State* state, int index, ValueReadState& readState, std::size_t depth) {
    switch (lua_type(state, index)) {
    case LUA_TBOOLEAN:
        consume_buffered_bytes(
            *readState.bufferedBytes,
            sizeof(LuaValue),
            readState.maxBufferedBytes,
            LuaExecutionStatus::InvalidProposal,
            "Lua values exceed buffered data limit"
        );
        return LuaValue(lua_toboolean(state, index) != 0);
    case LUA_TNUMBER:
        consume_buffered_bytes(
            *readState.bufferedBytes,
            sizeof(LuaValue),
            readState.maxBufferedBytes,
            LuaExecutionStatus::InvalidProposal,
            "Lua values exceed buffered data limit"
        );
        if (lua_isinteger(state, index))
            return LuaValue(static_cast<std::int64_t>(lua_tointeger(state, index)));

        if (!std::isfinite(lua_tonumber(state, index)))
            throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "Lua values cannot contain NaN or infinity");

        return LuaValue(static_cast<double>(lua_tonumber(state, index)));
    case LUA_TSTRING: {
        std::size_t length = 0;
        const char* text = lua_tolstring(state, index, &length);
        if (length > readState.maxStringBytes)
            throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "Lua value exceeds string size limit");
        consume_buffered_bytes(
            *readState.bufferedBytes,
            sizeof(LuaValue),
            readState.maxBufferedBytes,
            LuaExecutionStatus::InvalidProposal,
            "Lua values exceed buffered data limit"
        );
        consume_buffered_bytes(
            *readState.bufferedBytes,
            length,
            readState.maxBufferedBytes,
            LuaExecutionStatus::InvalidProposal,
            "Lua values exceed buffered data limit"
        );
        return LuaValue(std::string(text, length));
    }
    case LUA_TTABLE:
        return read_lua_table(state, index, readState, depth);
    default:
        throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "Lua value contains an unsupported type");
    }
}

void measure_lua_value(
    const LuaValue& value,
    std::size_t depth,
    std::size_t& entries,
    std::size_t& bufferedBytes,
    const LuaExecutionLimits& limits
) {
    if (depth > limits.maxTableDepth)
        throw ExecutionFailure(LuaExecutionStatus::HostError, "encoded component exceeds table depth limit");

    consume_buffered_bytes(
        bufferedBytes,
        sizeof(LuaValue),
        limits.maxBufferedValueBytes,
        LuaExecutionStatus::HostError,
        "encoded components exceed buffered data limit"
    );

    const LuaValue::Storage& storage = value.storage();
    if (const std::string* stored = std::get_if<std::string>(&storage)) {
        if (stored->size() > limits.maxStringBytes)
            throw ExecutionFailure(LuaExecutionStatus::HostError, "encoded component exceeds string size limit");

        consume_buffered_bytes(
            bufferedBytes,
            stored->size(),
            limits.maxBufferedValueBytes,
            LuaExecutionStatus::HostError,
            "encoded components exceed buffered data limit"
        );
        return;
    }

    if (const LuaValue::Array* stored = std::get_if<LuaValue::Array>(&storage)) {
        for (const LuaValue& child : *stored) {
            entries++;
            if (entries > limits.maxTableEntries)
                throw ExecutionFailure(LuaExecutionStatus::HostError, "encoded component exceeds table entry limit");

            measure_lua_value(child, depth + 1, entries, bufferedBytes, limits);
        }
        return;
    }

    if (const LuaValue::Table* stored = std::get_if<LuaValue::Table>(&storage)) {
        for (const auto& [key, child] : *stored) {
            if (key.size() > limits.maxStringBytes)
                throw ExecutionFailure(LuaExecutionStatus::HostError, "encoded component key exceeds string size limit");

            entries++;
            if (entries > limits.maxTableEntries)
                throw ExecutionFailure(LuaExecutionStatus::HostError, "encoded component exceeds table entry limit");

            consume_buffered_bytes(
                bufferedBytes,
                key.size(),
                limits.maxBufferedValueBytes,
                LuaExecutionStatus::HostError,
                "encoded components exceed buffered data limit"
            );
            measure_lua_value(child, depth + 1, entries, bufferedBytes, limits);
        }
    }
}

void push_lua_value(
    lua_State* state,
    const LuaValue& value,
    std::size_t depth,
    std::size_t& entries,
    const LuaExecutionLimits& limits
) {
    if (depth > limits.maxTableDepth)
        throw ExecutionFailure(LuaExecutionStatus::HostError, "encoded component exceeds table depth limit");

    const LuaValue::Storage& storage = value.storage();

    if (const bool* stored = std::get_if<bool>(&storage)) {
        lua_pushboolean(state, *stored);
        return;
    }

    if (const std::int64_t* stored = std::get_if<std::int64_t>(&storage)) {
        lua_pushinteger(state, static_cast<lua_Integer>(*stored));
        return;
    }

    if (const double* stored = std::get_if<double>(&storage)) {
        if (!std::isfinite(*stored))
            throw ExecutionFailure(LuaExecutionStatus::HostError, "encoded component contains NaN or infinity");

        lua_pushnumber(state, static_cast<lua_Number>(*stored));
        return;
    }

    if (const std::string* stored = std::get_if<std::string>(&storage)) {
        if (stored->size() > limits.maxStringBytes)
            throw ExecutionFailure(LuaExecutionStatus::HostError, "encoded component exceeds string size limit");
        lua_pushlstring(state, stored->data(), stored->size());
        return;
    }

    if (const LuaValue::Array* stored = std::get_if<LuaValue::Array>(&storage)) {
        if (stored->size() > limits.maxTableEntries || stored->size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw ExecutionFailure(LuaExecutionStatus::HostError, "encoded component exceeds table entry limit");
        lua_createtable(state, static_cast<int>(stored->size()), 0);
        int tableIndex = lua_absindex(state, -1);
        lua_createtable(state, 0, 1);
        lua_pushboolean(state, 1);
        lua_rawsetp(state, -2, &ArrayMetatableRegistryKey);
        lua_setmetatable(state, tableIndex);

        for (std::size_t i = 0; i < stored->size(); ++i) {
            entries++;
            if (entries > limits.maxTableEntries)
                throw ExecutionFailure(LuaExecutionStatus::HostError, "encoded component exceeds table entry limit");

            push_lua_value(state, stored->at(i), depth + 1, entries, limits);
            lua_rawseti(state, tableIndex, static_cast<lua_Integer>(i + 1));
        }

        return;
    }

    const auto& stored = std::get<LuaValue::Table>(storage);
    if (stored.size() > limits.maxTableEntries || stored.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw ExecutionFailure(LuaExecutionStatus::HostError, "encoded component exceeds table entry limit");
    lua_createtable(state, 0, static_cast<int>(stored.size()));
    int tableIndex = lua_absindex(state, -1);

    for (const auto& [key, child] : stored) {
        if (key.size() > limits.maxStringBytes)
            throw ExecutionFailure(LuaExecutionStatus::HostError, "encoded component key exceeds string size limit");
        entries++;
        if (entries > limits.maxTableEntries)
            throw ExecutionFailure(LuaExecutionStatus::HostError, "encoded component exceeds table entry limit");

        push_lua_value(state, child, depth + 1, entries, limits);
        raw_set_string(state, tableIndex, key);
    }
}

}

struct LuaBehaviorRunner::Impl {
    struct CachedCapabilities {
        BehaviorAccessRevision revision = 0;
        std::vector<CapabilityDescription> descriptions;
    };

    struct ExecutionCapability {
        CapabilityDescription description;
        std::optional<LuaValue> snapshot;
    };

    struct ExecutionContext {
        Impl* runner = nullptr;
        IntentTime now = 0;
        std::vector<ExecutionCapability> capabilities;
        std::vector<std::unique_ptr<PendingIntent>> pending;
        LuaExecutionStatus stickyStatus = LuaExecutionStatus::Success;
        std::string stickyDiagnostic;
        std::size_t instructions = 0;
        std::size_t instructionStep = InstructionHookStep;
        bool instructionExceeded = false;
        std::size_t bufferedValueBytes = 0;
    };

    LuaExecutionLimits limits;
    std::vector<Binding> bindings;
    std::map<std::pair<WorldInstanceId, BehaviorId>, CachedCapabilities> cache;
    std::optional<WorldInstanceId> cachedWorld;
    bool started = false;

    explicit Impl(LuaExecutionLimits executionLimits)
        : limits(executionLimits)
    {
    }

    void set_sticky(ExecutionContext& context, LuaExecutionStatus status, std::string_view message) {
        if (context.stickyStatus != LuaExecutionStatus::Success)
            return;

        context.stickyStatus = status;
        context.stickyDiagnostic = bounded_diagnostic(message, limits.maxDiagnosticBytes);
    }

    const std::vector<CapabilityDescription>& capabilities_for(World& world, BehaviorId owner) {
        if (!cachedWorld || *cachedWorld != world.instance_id()) {
            cache.clear();
            cachedWorld = world.instance_id();
        }

        BehaviorAccessRevision revision = world.behavior_access_revision(owner);
        auto key = std::make_pair(world.instance_id(), owner);
        auto found = cache.find(key);

        if (found != cache.end() && found->second.revision == revision)
            return found->second.descriptions;

        CachedCapabilities refreshed;
        refreshed.revision = revision;

        for (std::size_t i = 0; i < bindings.size(); ++i) {
            std::vector<CapabilityDescription> descriptions = bindings[i].describe(world, owner, i);
            refreshed.descriptions.insert(
                refreshed.descriptions.end(),
                std::make_move_iterator(descriptions.begin()),
                std::make_move_iterator(descriptions.end())
            );
        }

        auto [position, inserted] = cache.insert_or_assign(key, std::move(refreshed));
        (void)inserted;
        return position->second.descriptions;
    }

    static ExecutionContext& context_from_upvalue(lua_State* state) {
        return *static_cast<ExecutionContext*>(lua_touserdata(state, lua_upvalueindex(1)));
    }

    static ExecutionContext& context_from_state(lua_State* state) {
        return **static_cast<ExecutionContext**>(lua_getextraspace(state));
    }

    static void instruction_hook(lua_State* state, lua_Debug* debug) noexcept {
        (void)debug;
        auto** stored = static_cast<ExecutionContext**>(lua_getextraspace(state));
        ExecutionContext* context = *stored;

        if (!context)
            return;

        std::size_t remaining = context->runner->limits.maxInstructions - context->instructions;
        if (context->instructionStep <= remaining) {
            context->instructions += context->instructionStep;
            return;
        }

        context->instructionExceeded = true;
        lua_sethook(state, instruction_hook, LUA_MASKCOUNT, 1);
        lua_pushliteral(state, "Lua instruction limit exceeded");
        lua_error(state);
    }

    static bool allowed_request_field(std::string_view field) {
        return field == "value" || field == "priority" || field == "lifetime" || field == "duration_ms";
    }

    static void validate_request_fields(lua_State* state, int requestIndex) {
        requestIndex = lua_absindex(state, requestIndex);
        lua_pushnil(state);

        while (lua_next(state, requestIndex) != 0) {
            if (lua_type(state, -2) != LUA_TSTRING) {
                lua_pop(state, 2);
                throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "intent request keys must be strings");
            }

            std::size_t length = 0;
            const char* field = lua_tolstring(state, -2, &length);
            bool allowed = allowed_request_field(std::string_view(field, length));
            lua_pop(state, 1);

            if (!allowed) {
                lua_pop(state, 1);
                throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "intent request contains an unknown field");
            }
        }
    }

    static IntentPriority read_priority(lua_State* state, int requestIndex) {
        raw_get_string(state, requestIndex, "priority");

        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            return IntentPriority::Medium;
        }

        if (lua_type(state, -1) != LUA_TSTRING) {
            lua_pop(state, 1);
            throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "intent priority must be low, medium, or high");
        }

        std::size_t length = 0;
        const char* priority = lua_tolstring(state, -1, &length);
        std::string_view value(priority, length);
        lua_pop(state, 1);

        if (value == "low")
            return IntentPriority::Low;
        if (value == "medium")
            return IntentPriority::Medium;
        if (value == "high")
            return IntentPriority::High;

        throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "intent priority must be low, medium, or high");
    }

    static IntentLifetime read_lifetime(lua_State* state, int requestIndex, IntentTime now) {
        raw_get_string(state, requestIndex, "lifetime");
        bool hasLifetime = !lua_isnil(state, -1);

        if (hasLifetime) {
            if (lua_type(state, -1) != LUA_TSTRING) {
                lua_pop(state, 1);
                throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "intent lifetime must be persistent");
            }

            std::size_t length = 0;
            const char* lifetime = lua_tolstring(state, -1, &length);
            if (std::string_view(lifetime, length) != "persistent") {
                lua_pop(state, 1);
                throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "intent lifetime must be persistent");
            }
        }
        lua_pop(state, 1);

        raw_get_string(state, requestIndex, "duration_ms");
        bool hasDuration = !lua_isnil(state, -1);

        if (hasLifetime && hasDuration) {
            lua_pop(state, 1);
            throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "intent cannot specify both lifetime and duration_ms");
        }

        if (!hasDuration) {
            lua_pop(state, 1);
            return IntentLifetime::persistent();
        }

        if (!lua_isinteger(state, -1)) {
            lua_pop(state, 1);
            throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "intent duration_ms must be a nonnegative integer");
        }

        lua_Integer rawDuration = lua_tointeger(state, -1);
        lua_pop(state, 1);

        if (rawDuration < 0)
            throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "intent duration_ms must be a nonnegative integer");

        IntentTime duration = static_cast<IntentTime>(rawDuration);
        IntentTime maxScriptTime = static_cast<IntentTime>(std::numeric_limits<lua_Integer>::max());
        if (duration > maxScriptTime - now)
            throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "intent duration_ms overflows host time");

        return IntentLifetime::until_time(now + duration);
    }

    static void append_proposal(lua_State* state, ExecutionContext& context, std::size_t capabilityIndex) {
        if (lua_gettop(state) != 1 || lua_type(state, 1) != LUA_TTABLE)
            throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "propose expects one request table");

        if (capabilityIndex >= context.capabilities.size())
            throw ExecutionFailure(LuaExecutionStatus::HostError, "Lua capability index is invalid");

        const ExecutionCapability& capability = context.capabilities[capabilityIndex];
        if (!capability.description.writable)
            throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "component write access denied");

        if (context.pending.size() >= context.runner->limits.maxCreatedIntents)
            throw ExecutionFailure(LuaExecutionStatus::IntentLimitExceeded, "Lua intent limit exceeded");

        validate_request_fields(state, 1);
        IntentPriority priority = read_priority(state, 1);
        IntentLifetime lifetime = read_lifetime(state, 1, context.now);

        raw_get_string(state, 1, "value");
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            throw ExecutionFailure(LuaExecutionStatus::InvalidProposal, "intent request requires a value");
        }

        ValueReadState readState;
        readState.maxEntries = context.runner->limits.maxTableEntries;
        readState.maxDepth = context.runner->limits.maxTableDepth;
        readState.maxStringBytes = context.runner->limits.maxStringBytes;
        readState.bufferedBytes = &context.bufferedValueBytes;
        readState.maxBufferedBytes = context.runner->limits.maxBufferedValueBytes;
        LuaValue value = read_lua_value(state, -1, readState, 0);
        lua_pop(state, 1);

        const Binding& binding = context.runner->bindings.at(capability.description.binding);
        context.pending.push_back(binding.makePending(
            capability.description.name,
            value,
            lifetime,
            priority
        ));
    }

    static int propose_callback(lua_State* state) noexcept {
        ExecutionContext& context = context_from_upvalue(state);
        std::size_t capabilityIndex = static_cast<std::size_t>(lua_tointeger(state, lua_upvalueindex(2)));

        try {
            append_proposal(state, context, capabilityIndex);
            return 0;
        } catch (const ExecutionFailure& failure) {
            context.runner->set_sticky(context, failure.status, failure.what());
        } catch (const std::exception& exception) {
            context.runner->set_sticky(context, LuaExecutionStatus::InvalidProposal, exception.what());
        } catch (...) {
            context.runner->set_sticky(context, LuaExecutionStatus::InvalidProposal, "unknown intent proposal error");
        }

        const std::string& diagnostic = context.stickyDiagnostic;
        lua_pushlstring(state, diagnostic.data(), diagnostic.size());
        return lua_error(state);
    }

    static void add_allowed_base(lua_State* state, int environmentIndex) {
        luaL_requiref(state, "_G", luaopen_base, 0);
        int baseIndex = lua_absindex(state, -1);

        for (const char* name : {"assert", "error", "ipairs", "next", "pairs", "select", "tonumber", "type"})
            copy_raw_field(state, baseIndex, environmentIndex, name);

        lua_pop(state, 1);
    }

    static void add_library(
        lua_State* state,
        int environmentIndex,
        const char* name,
        lua_CFunction openFunction
    ) {
        luaL_requiref(state, name, openFunction, 0);
        raw_set_string(state, environmentIndex, name);
    }

    static void build_environment(lua_State* state, ExecutionContext& context) {
        lua_createtable(state, 0, 16);
        int environmentIndex = lua_absindex(state, -1);
        add_allowed_base(state, environmentIndex);

        add_library(state, environmentIndex, LUA_TABLIBNAME, luaopen_table);

        luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 0);
        remove_raw_field(state, -1, "dump");
        remove_raw_field(state, -1, "find");
        remove_raw_field(state, -1, "format");
        remove_raw_field(state, -1, "gmatch");
        remove_raw_field(state, -1, "gsub");
        remove_raw_field(state, -1, "match");
        raw_set_string(state, environmentIndex, LUA_STRLIBNAME);

        luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 0);
        remove_raw_field(state, -1, "random");
        remove_raw_field(state, -1, "randomseed");
        raw_set_string(state, environmentIndex, LUA_MATHLIBNAME);

        add_library(state, environmentIndex, LUA_UTF8LIBNAME, luaopen_utf8);

        lua_pushinteger(state, static_cast<lua_Integer>(context.now));
        raw_set_string(state, environmentIndex, "now_ms");

        lua_createtable(state, 0, static_cast<int>(context.runner->bindings.size()));
        int accessIndex = lua_absindex(state, -1);
        std::size_t capabilityIndex = 0;

        for (std::size_t bindingIndex = 0; bindingIndex < context.runner->bindings.size(); ++bindingIndex) {
            const Binding& binding = context.runner->bindings[bindingIndex];
            std::size_t firstCapability = capabilityIndex;

            while (
                capabilityIndex < context.capabilities.size()
                && context.capabilities[capabilityIndex].description.binding == bindingIndex
            )
                capabilityIndex++;

            std::size_t capabilityCount = capabilityIndex - firstCapability;
            if (capabilityCount == 0)
                continue;

            lua_createtable(state, 0, static_cast<int>(capabilityCount));
            int typeTableIndex = lua_absindex(state, -1);

            for (std::size_t current = firstCapability; current < capabilityIndex; ++current) {
                const ExecutionCapability& capability = context.capabilities[current];
                lua_createtable(state, 0, 2);
                int componentIndex = lua_absindex(state, -1);

                if (capability.snapshot) {
                    std::size_t entries = 0;
                    push_lua_value(state, *capability.snapshot, 0, entries, context.runner->limits);
                    raw_set_string(state, componentIndex, "value");
                }

                if (capability.description.writable) {
                    lua_pushlightuserdata(state, &context);
                    lua_pushinteger(state, static_cast<lua_Integer>(current));
                    lua_pushcclosure(state, propose_callback, 2);
                    raw_set_string(state, componentIndex, "propose");
                }

                raw_set_string(state, typeTableIndex, capability.description.name);
            }

            raw_set_string(state, accessIndex, binding.scriptName);
        }

        raw_set_string(state, environmentIndex, "access");
        lua_pushvalue(state, environmentIndex);
        lua_rawsetp(state, LUA_REGISTRYINDEX, &EnvironmentRegistryKey);
        lua_pop(state, 1);
    }

    static int bootstrap_callback(lua_State* state) noexcept {
        ExecutionContext& context = context_from_state(state);

        try {
            build_environment(state, context);
            return 0;
        } catch (const ExecutionFailure& failure) {
            context.runner->set_sticky(context, failure.status, failure.what());
        } catch (const std::exception& exception) {
            context.runner->set_sticky(context, LuaExecutionStatus::HostError, exception.what());
        } catch (...) {
            context.runner->set_sticky(context, LuaExecutionStatus::HostError, "unknown Lua environment error");
        }

        const std::string& diagnostic = context.stickyDiagnostic;
        lua_pushlstring(state, diagnostic.data(), diagnostic.size());
        return lua_error(state);
    }

    LuaExecutionResult failure_result(
        LuaExecutionStatus status,
        std::string_view diagnostic
    ) const {
        return {status, bounded_diagnostic(diagnostic, limits.maxDiagnosticBytes), {}};
    }

    LuaExecutionResult lua_error_result(
        lua_State* state,
        ExecutionContext& context,
        LuaMemoryBudget& memory,
        LuaExecutionStatus fallbackStatus
    ) {
        if (memory.exceeded)
            return failure_result(LuaExecutionStatus::MemoryLimitExceeded, "Lua memory limit exceeded");
        if (context.instructionExceeded)
            return failure_result(LuaExecutionStatus::InstructionLimitExceeded, "Lua instruction limit exceeded");
        if (context.stickyStatus != LuaExecutionStatus::Success)
            return failure_result(context.stickyStatus, context.stickyDiagnostic);

        if (lua_type(state, -1) != LUA_TSTRING)
            return failure_result(fallbackStatus, "Lua returned a non-string error");

        std::size_t length = 0;
        const char* message = lua_tolstring(state, -1, &length);
        return failure_result(fallbackStatus, std::string_view(message, length));
    }

    LuaExecutionResult run(World& world, BehaviorId owner, IntentTime now, std::string_view source) {
        started = true;

        if (!world.behavior_exists(owner))
            return failure_result(LuaExecutionStatus::InvalidBehavior, "behavior id not found");
        if (now > static_cast<IntentTime>(std::numeric_limits<lua_Integer>::max()))
            return failure_result(LuaExecutionStatus::HostError, "Lua time exceeds signed integer range");
        if (source.size() > limits.maxSourceBytes)
            return failure_result(LuaExecutionStatus::SourceLimitExceeded, "Lua source exceeds size limit");
        if (source.find('\0') != std::string_view::npos)
            return failure_result(LuaExecutionStatus::SyntaxError, "Lua source contains an embedded NUL byte");

        ExecutionContext context;
        context.runner = this;
        context.now = now;

        try {
            const auto& descriptions = capabilities_for(world, owner);
            context.capabilities.reserve(descriptions.size());

            for (const CapabilityDescription& description : descriptions) {
                ExecutionCapability capability;
                capability.description = description;

                if (description.readable) {
                    const Binding& binding = bindings.at(description.binding);
                    capability.snapshot = binding.snapshot(world, owner, description.name);
                    std::size_t entries = 0;
                    measure_lua_value(
                        *capability.snapshot,
                        0,
                        entries,
                        context.bufferedValueBytes,
                        limits
                    );
                }

                context.capabilities.push_back(std::move(capability));
            }
        } catch (const std::exception& exception) {
            return failure_result(LuaExecutionStatus::HostError, exception.what());
        } catch (...) {
            return failure_result(LuaExecutionStatus::HostError, "unknown capability snapshot error");
        }

        LuaMemoryBudget memory;
        memory.limit = limits.maxMemoryBytes;
        lua_State* state = lua_newstate(budget_allocator, &memory);

        if (!state)
            return failure_result(LuaExecutionStatus::MemoryLimitExceeded, "Lua state could not be created within memory limit");

        *static_cast<ExecutionContext**>(lua_getextraspace(state)) = &context;

        lua_pushcfunction(state, bootstrap_callback);
        int bootstrapStatus = lua_pcall(state, 0, 0, 0);
        if (bootstrapStatus != LUA_OK) {
            LuaExecutionResult result = lua_error_result(state, context, memory, LuaExecutionStatus::HostError);
            lua_close(state);
            return result;
        }

        int loadStatus = luaL_loadbufferx(state, source.data(), source.size(), "behavior", "t");
        if (loadStatus != LUA_OK) {
            LuaExecutionResult result = lua_error_result(state, context, memory, LuaExecutionStatus::SyntaxError);
            lua_close(state);
            return result;
        }

        lua_rawgetp(state, LUA_REGISTRYINDEX, &EnvironmentRegistryKey);
        if (!lua_setupvalue(state, -2, 1)) {
            lua_pop(state, 1);
            lua_close(state);
            return failure_result(LuaExecutionStatus::HostError, "Lua chunk has no environment upvalue");
        }

        int hookStep = limits.maxInstructions < static_cast<std::size_t>(InstructionHookStep)
            ? 1
            : InstructionHookStep;
        context.instructionStep = static_cast<std::size_t>(hookStep);
        lua_sethook(state, instruction_hook, LUA_MASKCOUNT, hookStep);
        int callStatus = lua_pcall(state, 0, 0, 0);
        lua_sethook(state, nullptr, 0, 0);

        if (callStatus != LUA_OK || context.stickyStatus != LuaExecutionStatus::Success || context.instructionExceeded) {
            LuaExecutionResult result = lua_error_result(state, context, memory, LuaExecutionStatus::RuntimeError);
            lua_close(state);
            return result;
        }

        lua_close(state);

        std::vector<IntentId> created;
        try {
            created.reserve(context.pending.size());
            for (const auto& pending : context.pending)
                created.push_back(pending->commit(world, owner));
        } catch (const std::exception& exception) {
            for (auto id = created.rbegin(); id != created.rend(); ++id) {
                if (world.intent_exists(*id))
                    world.destroy_intent(*id);
            }

            return failure_result(LuaExecutionStatus::CommitFailed, exception.what());
        } catch (...) {
            for (auto id = created.rbegin(); id != created.rend(); ++id) {
                if (world.intent_exists(*id))
                    world.destroy_intent(*id);
            }

            return failure_result(LuaExecutionStatus::CommitFailed, "unknown intent commit error");
        }

        return {LuaExecutionStatus::Success, {}, std::move(created)};
    }
};

LuaBehaviorRunner::LuaBehaviorRunner(LuaExecutionLimits limits)
    : impl(std::make_unique<Impl>(limits))
{
}

LuaBehaviorRunner::~LuaBehaviorRunner() = default;

void LuaBehaviorRunner::register_binding(Binding binding) {
    if (impl->started)
        throw std::logic_error("Lua component bindings are frozen after first execution");
    if (binding.scriptName.empty())
        throw std::invalid_argument("Lua component binding name cannot be empty");
    if (binding.scriptName.find('\0') != std::string::npos)
        throw std::invalid_argument("Lua component binding name cannot contain NUL");
    if (binding.type == InvalidComponentTypeId)
        throw std::invalid_argument("Lua component binding type is invalid");

    for (const Binding& existing : impl->bindings) {
        if (existing.scriptName == binding.scriptName)
            throw std::runtime_error("Lua component binding name already registered");
        if (existing.type == binding.type)
            throw std::runtime_error("Lua component type already registered");
    }

    impl->bindings.push_back(std::move(binding));
}

LuaExecutionResult LuaBehaviorRunner::execute(
    World& world,
    BehaviorId owner,
    IntentTime now,
    std::string_view source
) {
    LuaExecutionResult result;
    try {
        result = impl->run(world, owner, now, source);
    } catch (const std::exception& exception) {
        result = impl->failure_result(LuaExecutionStatus::HostError, exception.what());
    } catch (...) {
        result = impl->failure_result(LuaExecutionStatus::HostError, "unknown Lua host error");
    }
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offset;
    for (const unsigned char byte : source) {
        hash ^= byte;
        hash *= prime;
    }
    static constexpr char digits[] = "0123456789abcdef";
    std::string sourceHash(16, '0');
    for (std::size_t index = 0; index < sourceHash.size(); ++index) {
        sourceHash[sourceHash.size() - index - 1] = digits[hash & 0x0fU];
        hash >>= 4U;
    }
    world.record_script_execution(ScriptExecutionEvidence{
        owner,
        now,
        impl->limits.recordFullSource ? std::string{source} : std::string{},
        "fnv1a64:" + sourceHash,
        impl->limits.recordFullSource,
        static_cast<std::uint32_t>(result.status),
        result.diagnostic,
        result.createdIntents.size()
    });
    return result;
}

}
