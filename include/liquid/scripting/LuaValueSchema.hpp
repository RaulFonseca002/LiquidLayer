#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace liquid::scripting {

// Complete in LuaBehaviorRunner.hpp. Forward-declared so this header does not
// include the runner header, which includes this one.
class LuaValue;
class LuaCapabilityManifest;

// Fixed L0 ceilings (docs/LIQUID_L0_IMPLEMENTATION_SPEC.md "Exact schemas and limits").
inline constexpr std::size_t LuaSchemaMaxContainerNesting = 16;
inline constexpr std::size_t LuaSchemaMaxNodes = 4'096;
inline constexpr std::size_t LuaSchemaMaxFields = 256;
inline constexpr std::size_t LuaSchemaMaxArrayItems = 4'096;
inline constexpr std::size_t LuaSchemaMaxStringBytes = 65'536;
inline constexpr std::size_t LuaSchemaMaxEnumEntries = 256;
inline constexpr std::size_t LuaSchemaMaxEnumBytes = 65'536;
inline constexpr std::size_t LuaMaxDescriptionBytes = 1'024;
inline constexpr std::size_t LuaMaxNameBytes = 256;
inline constexpr std::size_t LuaMaxValueNodes = 16'384;
inline constexpr std::size_t LuaMaxSchemaDiagnosticBytes = 4'096;

enum class LuaSchemaKind {
    Boolean,
    Integer,
    Number,
    String,
    Array,
    Object
};

struct LuaSchemaField;

// Immutable, value-like schema tree. Copies share const nodes; every factory
// validates the complete expanded tree and throws std::invalid_argument.
class LuaValueSchema {
public:
    static LuaValueSchema boolean(std::string description = {});
    static LuaValueSchema integer(
        std::optional<std::int64_t> minimum,
        std::optional<std::int64_t> maximum,
        std::string description = {}
    );
    static LuaValueSchema number(
        std::optional<double> minimum,
        std::optional<double> maximum,
        std::string description = {}
    );
    static LuaValueSchema string(
        std::size_t minimumBytes,
        std::size_t maximumBytes,
        std::optional<std::vector<std::string>> enumeration = std::nullopt,
        std::string description = {}
    );
    static LuaValueSchema array(
        LuaValueSchema item,
        std::size_t minimumItems,
        std::size_t maximumItems,
        std::string description = {}
    );
    static LuaValueSchema object(std::vector<LuaSchemaField> fields, std::string description = {});

    LuaSchemaKind kind() const;
    const std::string& description() const;

    std::optional<std::int64_t> integer_minimum() const;
    std::optional<std::int64_t> integer_maximum() const;
    std::optional<double> number_minimum() const;
    std::optional<double> number_maximum() const;
    std::size_t minimum_bytes() const;
    std::size_t maximum_bytes() const;
    const std::optional<std::vector<std::string>>& enumeration() const;
    const LuaValueSchema& item() const;
    std::size_t minimum_items() const;
    std::size_t maximum_items() const;
    // Sorted by unsigned-byte lexical name order.
    const std::vector<LuaSchemaField>& fields() const;

    // Totals over the expanded tree.
    std::size_t node_count() const;
    std::size_t container_depth() const;
    std::size_t enum_bytes() const;
    std::size_t description_bytes() const;
    // Contract logical payload bytes: tags, bounds, enum entries, field
    // names and descriptions of every node.
    std::size_t logical_bytes() const;

    struct Node;

private:
    std::shared_ptr<const Node> node;

    explicit LuaValueSchema(std::shared_ptr<const Node> root);

    // The one Lua index-segment renderer: `.name` for ASCII non-keyword
    // identifiers, otherwise `["..."]` with `\ddd` escapes.
    static std::string lua_index_segment(std::string_view name);

    friend class LuaCapabilityManifest;
};

struct LuaSchemaField {
    std::string name;
    LuaValueSchema schema;
    bool required = true;
};

class LuaModelBindingMetadata {
public:
    LuaModelBindingMetadata(
        LuaValueSchema readSchema,
        LuaValueSchema writeSchema,
        std::string description = {}
    );

    const LuaValueSchema& read_schema() const;
    const LuaValueSchema& write_schema() const;
    const std::string& description() const;

private:
    LuaValueSchema readSchema;
    LuaValueSchema writeSchema;
    std::string bindingDescription;
};

LuaModelBindingMetadata symmetric_metadata(LuaValueSchema schema, std::string description = {});

enum class LuaSchemaErrorCode {
    Kind,
    Range,
    MissingField,
    UnknownField,
    Limit
};

struct LuaSchemaError {
    LuaSchemaErrorCode code = LuaSchemaErrorCode::Kind;
    // Lua-style path from the root (`.level`, `["a b"]`, `[2]`); empty for the root.
    std::string path;
    std::string diagnostic;
};

struct LuaSchemaValidation {
    std::optional<LuaSchemaError> error;

    bool valid() const;
};

LuaSchemaValidation validate_lua_value(const LuaValueSchema& schema, const LuaValue& value);

}
