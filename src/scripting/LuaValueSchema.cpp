#include "liquid/scripting/LuaValueSchema.hpp"
#include "liquid/scripting/LuaBehaviorRunner.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace liquid::scripting {

// Schema kinds are matched against LuaValue::Storage by variant index.
static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(LuaSchemaKind::Boolean), LuaValue::Storage>, bool>);
static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(LuaSchemaKind::Integer), LuaValue::Storage>, std::int64_t>);
static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(LuaSchemaKind::Number), LuaValue::Storage>, double>);
static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(LuaSchemaKind::String), LuaValue::Storage>, std::string>);
static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(LuaSchemaKind::Array), LuaValue::Storage>, LuaValue::Array>);
static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(LuaSchemaKind::Object), LuaValue::Storage>, LuaValue::Table>);

struct LuaValueSchema::Node {
    LuaSchemaKind kind = LuaSchemaKind::Boolean;
    std::string description;
    std::optional<std::int64_t> integerMinimum;
    std::optional<std::int64_t> integerMaximum;
    std::optional<double> numberMinimum;
    std::optional<double> numberMaximum;
    std::size_t minimumBytes = 0;
    std::size_t maximumBytes = 0;
    std::optional<std::vector<std::string>> enumeration;
    std::optional<LuaValueSchema> item;
    std::size_t minimumItems = 0;
    std::size_t maximumItems = 0;
    std::vector<LuaSchemaField> fields;
    std::size_t nodeCount = 0;
    std::size_t containerDepth = 0;
    std::size_t enumBytes = 0;
    std::size_t descriptionBytes = 0;
    std::size_t logicalBytes = 0;
};

namespace {

std::size_t checked_schema_add(std::size_t left, std::size_t right) {
    if (right > std::numeric_limits<std::size_t>::max() - left)
        throw std::invalid_argument("Lua schema totals overflow");
    return left + right;
}

// Rejects overlong forms, surrogates, code points above U+10FFFF and
// truncated sequences.
bool valid_utf8(std::string_view text) {
    std::size_t index = 0;

    while (index < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        std::size_t length = 0;
        std::uint32_t codePoint = 0;

        if (lead < 0x80) {
            ++index;
            continue;
        }
        if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
            codePoint = lead & 0x1Fu;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
            codePoint = lead & 0x0Fu;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            codePoint = lead & 0x07u;
        } else {
            return false;
        }

        if (text.size() - index < length)
            return false;

        for (std::size_t offset = 1; offset < length; ++offset) {
            const unsigned char next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xC0u) != 0x80u)
                return false;
            codePoint = (codePoint << 6) | (next & 0x3Fu);
        }

        if ((length == 3 && codePoint < 0x800u) || (length == 4 && codePoint < 0x10000u))
            return false;
        if (codePoint > 0x10FFFFu || (codePoint >= 0xD800u && codePoint <= 0xDFFFu))
            return false;

        index += length;
    }

    return true;
}

void validate_description(std::string_view description) {
    if (description.size() > LuaMaxDescriptionBytes)
        throw std::invalid_argument("Lua schema description exceeds 1024 bytes");
    if (description.find('\0') != std::string_view::npos)
        throw std::invalid_argument("Lua schema description cannot contain NUL");
    if (!valid_utf8(description))
        throw std::invalid_argument("Lua schema description must be valid UTF-8");
}

void add_child_totals(LuaValueSchema::Node& node, const LuaValueSchema& child) {
    node.nodeCount = checked_schema_add(node.nodeCount, child.node_count());
    node.containerDepth = std::max(node.containerDepth, checked_schema_add(child.container_depth(), 1));
    node.enumBytes = checked_schema_add(node.enumBytes, child.enum_bytes());
    node.descriptionBytes = checked_schema_add(node.descriptionBytes, child.description_bytes());
    node.logicalBytes = checked_schema_add(node.logicalBytes, child.logical_bytes());
}

// Contract logical accounting: an 8-byte kind tag, the description, 8 per
// present numeric bound, 8 + bytes per enum entry, and 8 + name bytes + 1
// (required flag) per object field. Children are added by add_child_totals.
std::size_t own_logical_bytes(const LuaValueSchema::Node& node) {
    std::size_t bytes = checked_schema_add(8, node.description.size());
    switch (node.kind) {
    case LuaSchemaKind::Boolean:
        break;
    case LuaSchemaKind::Integer:
        bytes += (node.integerMinimum ? 8 : 0) + (node.integerMaximum ? 8 : 0);
        break;
    case LuaSchemaKind::Number:
        bytes += (node.numberMinimum ? 8 : 0) + (node.numberMaximum ? 8 : 0);
        break;
    case LuaSchemaKind::String:
        bytes += 16;
        break;
    case LuaSchemaKind::Array:
        bytes += 16;
        break;
    case LuaSchemaKind::Object:
        for (const LuaSchemaField& field : node.fields)
            bytes = checked_schema_add(bytes, checked_schema_add(9, field.name.size()));
        break;
    }
    if (node.enumeration) {
        for (const std::string& entry : *node.enumeration)
            bytes = checked_schema_add(bytes, checked_schema_add(8, entry.size()));
    }
    return bytes;
}

// Validates the node description, computes the expanded-tree totals and
// enforces the per-schema L0 ceilings.
std::shared_ptr<const LuaValueSchema::Node> finish_node(LuaValueSchema::Node node) {
    validate_description(node.description);

    node.nodeCount = 1;
    node.containerDepth = 0;
    node.enumBytes = 0;
    node.descriptionBytes = node.description.size();
    node.logicalBytes = own_logical_bytes(node);

    if (node.enumeration) {
        for (const std::string& entry : *node.enumeration)
            node.enumBytes = checked_schema_add(node.enumBytes, entry.size());
    }
    if (node.kind == LuaSchemaKind::Array)
        add_child_totals(node, *node.item);
    if (node.kind == LuaSchemaKind::Object) {
        node.containerDepth = 1;
        for (const LuaSchemaField& field : node.fields)
            add_child_totals(node, field.schema);
    }

    if (node.nodeCount > LuaSchemaMaxNodes)
        throw std::invalid_argument("Lua schema exceeds 4096 expanded nodes");
    if (node.containerDepth > LuaSchemaMaxContainerNesting)
        throw std::invalid_argument("Lua schema exceeds 16 nested containers");
    if (node.enumBytes > LuaSchemaMaxEnumBytes)
        throw std::invalid_argument("Lua schema enum entries exceed 65536 aggregate bytes");

    return std::make_shared<const LuaValueSchema::Node>(std::move(node));
}

bool is_identifier_start(unsigned char byte) {
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || byte == '_';
}

bool is_identifier_byte(unsigned char byte) {
    return is_identifier_start(byte) || (byte >= '0' && byte <= '9');
}

bool is_lua_identifier(std::string_view name) {
    static constexpr std::array<std::string_view, 22> Keywords = {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if",
        "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"
    };

    if (name.empty() || !is_identifier_start(static_cast<unsigned char>(name.front())))
        return false;
    for (char byte : name) {
        if (!is_identifier_byte(static_cast<unsigned char>(byte)))
            return false;
    }
    return std::find(Keywords.begin(), Keywords.end(), name) == Keywords.end();
}

// The one Lua index-segment renderer, shared by schema paths and manifest
// access expressions through LuaValueSchema::lua_index_segment.
std::string render_index_segment(std::string_view name) {
    if (is_lua_identifier(name))
        return "." + std::string(name);

    std::string segment = "[\"";
    for (char character : name) {
        const unsigned char byte = static_cast<unsigned char>(character);

        if (byte == '"' || byte == '\\') {
            segment += '\\';
            segment += character;
        } else if (byte >= 0x20 && byte <= 0x7E) {
            segment += character;
        } else {
            segment += '\\';
            segment += static_cast<char>('0' + byte / 100);
            segment += static_cast<char>('0' + (byte / 10) % 10);
            segment += static_cast<char>('0' + byte % 10);
        }
    }
    segment += "\"]";
    return segment;
}

// New L0 diagnostics end in "..." when truncated.
std::string bounded_schema_text(std::string text) {
    if (text.size() <= LuaMaxSchemaDiagnosticBytes)
        return text;

    text.resize(LuaMaxSchemaDiagnosticBytes - 3);
    text += "...";
    return text;
}

const char* kind_name(std::size_t index) {
    static constexpr std::array<const char*, 6> Names = {
        "Boolean", "Integer", "Number", "String", "Array", "Object"
    };
    return index < Names.size() ? Names[index] : "unknown";
}

}

LuaValueSchema::LuaValueSchema(std::shared_ptr<const Node> root)
    : node(std::move(root))
{
}

LuaValueSchema LuaValueSchema::boolean(std::string description) {
    Node root;
    root.kind = LuaSchemaKind::Boolean;
    root.description = std::move(description);
    return LuaValueSchema(finish_node(std::move(root)));
}

LuaValueSchema LuaValueSchema::integer(
    std::optional<std::int64_t> minimum,
    std::optional<std::int64_t> maximum,
    std::string description
) {
    if (minimum && maximum && *minimum > *maximum)
        throw std::invalid_argument("Lua Integer schema minimum exceeds maximum");

    Node root;
    root.kind = LuaSchemaKind::Integer;
    root.description = std::move(description);
    root.integerMinimum = minimum;
    root.integerMaximum = maximum;
    return LuaValueSchema(finish_node(std::move(root)));
}

LuaValueSchema LuaValueSchema::number(
    std::optional<double> minimum,
    std::optional<double> maximum,
    std::string description
) {
    if ((minimum && !std::isfinite(*minimum)) || (maximum && !std::isfinite(*maximum)))
        throw std::invalid_argument("Lua Number schema bounds must be finite");
    if (minimum && maximum && *minimum > *maximum)
        throw std::invalid_argument("Lua Number schema minimum exceeds maximum");

    Node root;
    root.kind = LuaSchemaKind::Number;
    root.description = std::move(description);
    root.numberMinimum = minimum;
    root.numberMaximum = maximum;
    return LuaValueSchema(finish_node(std::move(root)));
}

LuaValueSchema LuaValueSchema::string(
    std::size_t minimumBytes,
    std::size_t maximumBytes,
    std::optional<std::vector<std::string>> enumeration,
    std::string description
) {
    if (minimumBytes > maximumBytes)
        throw std::invalid_argument("Lua String schema minimum exceeds maximum");
    if (maximumBytes > LuaSchemaMaxStringBytes)
        throw std::invalid_argument("Lua String schema maximum exceeds 65536 bytes");

    if (enumeration) {
        if (enumeration->empty())
            throw std::invalid_argument("Lua String schema enum cannot be empty");
        if (enumeration->size() > LuaSchemaMaxEnumEntries)
            throw std::invalid_argument("Lua String schema enum exceeds 256 entries");

        std::vector<std::string> sorted = *enumeration;
        std::sort(sorted.begin(), sorted.end());
        if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
            throw std::invalid_argument("Lua String schema enum contains a duplicate entry");
    }

    Node root;
    root.kind = LuaSchemaKind::String;
    root.description = std::move(description);
    root.minimumBytes = minimumBytes;
    root.maximumBytes = maximumBytes;
    root.enumeration = std::move(enumeration);
    return LuaValueSchema(finish_node(std::move(root)));
}

LuaValueSchema LuaValueSchema::array(
    LuaValueSchema item,
    std::size_t minimumItems,
    std::size_t maximumItems,
    std::string description
) {
    if (minimumItems > maximumItems)
        throw std::invalid_argument("Lua Array schema minimum exceeds maximum");
    if (maximumItems > LuaSchemaMaxArrayItems)
        throw std::invalid_argument("Lua Array schema maximum exceeds 4096 items");

    Node root;
    root.kind = LuaSchemaKind::Array;
    root.description = std::move(description);
    root.item = std::move(item);
    root.minimumItems = minimumItems;
    root.maximumItems = maximumItems;
    return LuaValueSchema(finish_node(std::move(root)));
}

LuaValueSchema LuaValueSchema::object(std::vector<LuaSchemaField> fields, std::string description) {
    if (fields.size() > LuaSchemaMaxFields)
        throw std::invalid_argument("Lua Object schema exceeds 256 fields");

    for (const LuaSchemaField& field : fields) {
        if (field.name.size() > LuaMaxNameBytes)
            throw std::invalid_argument("Lua Object schema field name exceeds 256 bytes");
    }

    std::sort(fields.begin(), fields.end(), [](const LuaSchemaField& left, const LuaSchemaField& right) {
        return left.name < right.name;
    });
    auto duplicate = std::adjacent_find(fields.begin(), fields.end(), [](const LuaSchemaField& left, const LuaSchemaField& right) {
        return left.name == right.name;
    });
    if (duplicate != fields.end())
        throw std::invalid_argument("Lua Object schema contains a duplicate field name");

    Node root;
    root.kind = LuaSchemaKind::Object;
    root.description = std::move(description);
    root.fields = std::move(fields);
    return LuaValueSchema(finish_node(std::move(root)));
}

LuaSchemaKind LuaValueSchema::kind() const {
    return node->kind;
}

const std::string& LuaValueSchema::description() const {
    return node->description;
}

std::optional<std::int64_t> LuaValueSchema::integer_minimum() const {
    return node->integerMinimum;
}

std::optional<std::int64_t> LuaValueSchema::integer_maximum() const {
    return node->integerMaximum;
}

std::optional<double> LuaValueSchema::number_minimum() const {
    return node->numberMinimum;
}

std::optional<double> LuaValueSchema::number_maximum() const {
    return node->numberMaximum;
}

std::size_t LuaValueSchema::minimum_bytes() const {
    return node->minimumBytes;
}

std::size_t LuaValueSchema::maximum_bytes() const {
    return node->maximumBytes;
}

const std::optional<std::vector<std::string>>& LuaValueSchema::enumeration() const {
    return node->enumeration;
}

const LuaValueSchema& LuaValueSchema::item() const {
    if (!node->item)
        throw std::logic_error("Lua schema is not an Array");
    return *node->item;
}

std::size_t LuaValueSchema::minimum_items() const {
    return node->minimumItems;
}

std::size_t LuaValueSchema::maximum_items() const {
    return node->maximumItems;
}

const std::vector<LuaSchemaField>& LuaValueSchema::fields() const {
    return node->fields;
}

std::size_t LuaValueSchema::node_count() const {
    return node->nodeCount;
}

std::size_t LuaValueSchema::container_depth() const {
    return node->containerDepth;
}

std::size_t LuaValueSchema::enum_bytes() const {
    return node->enumBytes;
}

std::size_t LuaValueSchema::description_bytes() const {
    return node->descriptionBytes;
}

std::size_t LuaValueSchema::logical_bytes() const {
    return node->logicalBytes;
}

std::string LuaValueSchema::lua_index_segment(std::string_view name) {
    return render_index_segment(name);
}

LuaModelBindingMetadata::LuaModelBindingMetadata(
    LuaValueSchema read,
    LuaValueSchema write,
    std::string description
)
    : readSchema(std::move(read)),
      writeSchema(std::move(write)),
      bindingDescription(std::move(description))
{
    validate_description(bindingDescription);
}

const LuaValueSchema& LuaModelBindingMetadata::read_schema() const {
    return readSchema;
}

const LuaValueSchema& LuaModelBindingMetadata::write_schema() const {
    return writeSchema;
}

const std::string& LuaModelBindingMetadata::description() const {
    return bindingDescription;
}

LuaModelBindingMetadata symmetric_metadata(LuaValueSchema schema, std::string description) {
    LuaValueSchema write = schema;
    return LuaModelBindingMetadata(std::move(schema), std::move(write), std::move(description));
}

bool LuaSchemaValidation::valid() const {
    return !error.has_value();
}

namespace {

// Depth-first walk that stops at the first failure. Recursion is bounded by
// the schema, which is at most 16 containers deep.
class SchemaWalk {
public:
    std::optional<LuaSchemaError> error;

    bool visit(const LuaValueSchema& schema, const LuaValue& value) {
        if (++visitedNodes > LuaMaxValueNodes)
            return fail(LuaSchemaErrorCode::Limit, "value exceeds 16384 nodes");

        const LuaValue::Storage& storage = value.storage();
        const std::size_t expected = static_cast<std::size_t>(schema.kind());
        if (storage.index() != expected) {
            return fail(
                LuaSchemaErrorCode::Kind,
                std::string("expected ") + kind_name(expected) + ", got " + kind_name(storage.index()));
        }

        switch (schema.kind()) {
        case LuaSchemaKind::Boolean:
            return true;
        case LuaSchemaKind::Integer:
            return visit_integer(schema, std::get<std::int64_t>(storage));
        case LuaSchemaKind::Number:
            return visit_number(schema, std::get<double>(storage));
        case LuaSchemaKind::String:
            return visit_string(schema, std::get<std::string>(storage));
        case LuaSchemaKind::Array:
            return visit_array(schema, std::get<LuaValue::Array>(storage));
        case LuaSchemaKind::Object:
            return visit_object(schema, std::get<LuaValue::Table>(storage));
        }
        return fail(LuaSchemaErrorCode::Kind, "unknown schema kind");
    }

private:
    std::size_t visitedNodes = 0;
    std::string path;

    bool fail(LuaSchemaErrorCode code, std::string_view message) {
        std::string diagnostic = "Lua value at " + (path.empty() ? std::string("root") : path) + ": ";
        diagnostic += message;
        error = LuaSchemaError{code, bounded_schema_text(path), bounded_schema_text(std::move(diagnostic))};
        return false;
    }

    bool fail_at(std::string_view segment, LuaSchemaErrorCode code, std::string_view message) {
        path += segment;
        return fail(code, message);
    }

    bool visit_integer(const LuaValueSchema& schema, std::int64_t value) {
        if ((schema.integer_minimum() && value < *schema.integer_minimum())
            || (schema.integer_maximum() && value > *schema.integer_maximum()))
            return fail(LuaSchemaErrorCode::Range, "integer is outside the declared bounds");
        return true;
    }

    bool visit_number(const LuaValueSchema& schema, double value) {
        if (!std::isfinite(value))
            return fail(LuaSchemaErrorCode::Kind, "expected a finite Number");
        if ((schema.number_minimum() && value < *schema.number_minimum())
            || (schema.number_maximum() && value > *schema.number_maximum()))
            return fail(LuaSchemaErrorCode::Range, "number is outside the declared bounds");
        return true;
    }

    bool visit_string(const LuaValueSchema& schema, const std::string& value) {
        if (value.size() < schema.minimum_bytes() || value.size() > schema.maximum_bytes())
            return fail(LuaSchemaErrorCode::Range, "string byte length is outside the declared bounds");

        const std::optional<std::vector<std::string>>& enumeration = schema.enumeration();
        if (enumeration && std::find(enumeration->begin(), enumeration->end(), value) == enumeration->end())
            return fail(LuaSchemaErrorCode::Range, "string is not a declared enum entry");
        return true;
    }

    bool visit_array(const LuaValueSchema& schema, const LuaValue::Array& items) {
        if (items.size() < schema.minimum_items() || items.size() > schema.maximum_items())
            return fail(LuaSchemaErrorCode::Range, "array length is outside the declared bounds");

        const std::size_t parentLength = path.size();
        for (std::size_t index = 0; index < items.size(); ++index) {
            path += "[" + std::to_string(index + 1) + "]";
            if (!visit(schema.item(), items[index]))
                return false;
            path.resize(parentLength);
        }
        return true;
    }

    bool visit_object(const LuaValueSchema& schema, const LuaValue::Table& table) {
        for (const auto& entry : table) {
            if (entry.first.size() > LuaMaxNameBytes)
                return fail(LuaSchemaErrorCode::Limit, "object key exceeds 256 bytes");
        }

        // Fields and table keys are both in unsigned-byte order; merge them.
        const std::vector<LuaSchemaField>& fields = schema.fields();
        auto field = fields.begin();
        auto entry = table.begin();
        const std::size_t parentLength = path.size();

        while (field != fields.end() || entry != table.end()) {
            if (entry != table.end() && (field == fields.end() || entry->first < field->name)) {
                return fail_at(
                    render_index_segment(entry->first),
                    LuaSchemaErrorCode::UnknownField,
                    "field is not declared by the schema");
            }

            if (entry == table.end() || field->name < entry->first) {
                if (field->required) {
                    return fail_at(
                        render_index_segment(field->name),
                        LuaSchemaErrorCode::MissingField,
                        "required field is absent");
                }
                ++field;
                continue;
            }

            path += render_index_segment(field->name);
            if (!visit(field->schema, entry->second))
                return false;
            path.resize(parentLength);
            ++field;
            ++entry;
        }
        return true;
    }
};

}

LuaSchemaValidation validate_lua_value(const LuaValueSchema& schema, const LuaValue& value) {
    SchemaWalk walk;
    walk.visit(schema, value);
    return LuaSchemaValidation{std::move(walk.error)};
}

}
