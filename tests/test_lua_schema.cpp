#include "liquid/scripting/LuaBehaviorRunner.hpp"
#include "liquid/scripting/LuaValueSchema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace liquid::scripting;

namespace {

using Schema = LuaValueSchema;

void require_valid(const LuaSchemaValidation& validation) {
    INFO((validation.error ? validation.error->diagnostic : std::string("valid")));
    REQUIRE(validation.valid());
    REQUIRE(!validation.error.has_value());
}

void require_error(
    const LuaSchemaValidation& validation,
    LuaSchemaErrorCode code,
    std::string_view path
) {
    REQUIRE(!validation.valid());
    REQUIRE(validation.error.has_value());
    REQUIRE(validation.error->code == code);
    REQUIRE(validation.error->path == path);
    REQUIRE(!validation.error->diagnostic.empty());
}

std::string field_name(std::size_t index) {
    std::string digits = std::to_string(index);
    return "f" + std::string(4 - digits.size(), '0') + digits;
}

// booleanFields + 1 expanded nodes.
Schema flat_object(std::size_t booleanFields) {
    std::vector<LuaSchemaField> fields;
    for (std::size_t index = 0; index < booleanFields; ++index)
        fields.push_back({field_name(index), Schema::boolean(), true});
    return Schema::object(std::move(fields));
}

Schema nested_arrays(std::size_t containers) {
    Schema schema = Schema::boolean();
    for (std::size_t level = 0; level < containers; ++level)
        schema = Schema::array(schema, 0, 1);
    return schema;
}

LuaValue nested_array_value(std::size_t containers) {
    LuaValue value{true};
    for (std::size_t level = 0; level < containers; ++level)
        value = LuaValue{LuaValue::Array{value}};
    return value;
}

std::vector<LuaValue> one_value_of_each_kind() {
    return {
        LuaValue{true},
        LuaValue{std::int64_t{1}},
        LuaValue{1.5},
        LuaValue{"text"},
        LuaValue{LuaValue::Array{}},
        LuaValue{LuaValue::Table{}}
    };
}

std::vector<Schema> one_schema_of_each_kind() {
    return {
        Schema::boolean(),
        Schema::integer(std::nullopt, std::nullopt),
        Schema::number(std::nullopt, std::nullopt),
        Schema::string(0, 16),
        Schema::array(Schema::boolean(), 0, 4),
        Schema::object({})
    };
}

}

TEST_CASE("L0.1 all six schema kinds accept conforming values") {
    require_valid(validate_lua_value(Schema::boolean(), LuaValue{false}));
    require_valid(validate_lua_value(
        Schema::integer(std::nullopt, std::nullopt), LuaValue{std::int64_t{-7}}));
    require_valid(validate_lua_value(Schema::number(std::nullopt, std::nullopt), LuaValue{0.25}));
    require_valid(validate_lua_value(Schema::string(0, 8), LuaValue{"lamp"}));
    require_valid(validate_lua_value(
        Schema::array(Schema::integer(std::nullopt, std::nullopt), 0, 4),
        LuaValue{LuaValue::Array{LuaValue{1}, LuaValue{2}}}));
    require_valid(validate_lua_value(
        Schema::object({{"level", Schema::integer(0, 100), true}}),
        LuaValue{LuaValue::Table{{"level", LuaValue{40}}}}));

    REQUIRE(Schema::boolean().kind() == LuaSchemaKind::Boolean);
    REQUIRE(Schema::integer(std::nullopt, std::nullopt).kind() == LuaSchemaKind::Integer);
    REQUIRE(Schema::number(std::nullopt, std::nullopt).kind() == LuaSchemaKind::Number);
    REQUIRE(Schema::string(0, 1).kind() == LuaSchemaKind::String);
    REQUIRE(Schema::array(Schema::boolean(), 0, 1).kind() == LuaSchemaKind::Array);
    REQUIRE(Schema::object({}).kind() == LuaSchemaKind::Object);
}

TEST_CASE("L0.1 each schema kind rejects every other LuaValue kind with Kind") {
    const std::vector<Schema> schemas = one_schema_of_each_kind();
    const std::vector<LuaValue> values = one_value_of_each_kind();

    for (std::size_t schemaIndex = 0; schemaIndex < schemas.size(); ++schemaIndex) {
        for (std::size_t valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
            INFO("schema " << schemaIndex << " value " << valueIndex);
            const LuaSchemaValidation validation =
                validate_lua_value(schemas[schemaIndex], values[valueIndex]);
            if (schemaIndex == valueIndex)
                require_valid(validation);
            else
                require_error(validation, LuaSchemaErrorCode::Kind, "");
        }
    }
}

TEST_CASE("L0.1 exact numbers keep 1 and 1.0 distinct and reject nonfinite values") {
    const Schema integer = Schema::integer(std::nullopt, std::nullopt);
    const Schema number = Schema::number(std::nullopt, std::nullopt);

    require_valid(validate_lua_value(integer, LuaValue{std::int64_t{1}}));
    require_error(validate_lua_value(integer, LuaValue{1.0}), LuaSchemaErrorCode::Kind, "");
    require_valid(validate_lua_value(number, LuaValue{1.0}));
    require_error(validate_lua_value(number, LuaValue{std::int64_t{1}}), LuaSchemaErrorCode::Kind, "");

    require_error(
        validate_lua_value(number, LuaValue{std::numeric_limits<double>::quiet_NaN()}),
        LuaSchemaErrorCode::Kind, "");
    require_error(
        validate_lua_value(number, LuaValue{std::numeric_limits<double>::infinity()}),
        LuaSchemaErrorCode::Kind, "");
}

TEST_CASE("L0.1 integer and number bounds are inclusive") {
    const Schema level = Schema::integer(0, 100);
    require_valid(validate_lua_value(level, LuaValue{0}));
    require_valid(validate_lua_value(level, LuaValue{100}));
    require_error(validate_lua_value(level, LuaValue{-1}), LuaSchemaErrorCode::Range, "");
    require_error(validate_lua_value(level, LuaValue{101}), LuaSchemaErrorCode::Range, "");

    const Schema extremes = Schema::integer(
        std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max());
    require_valid(validate_lua_value(extremes, LuaValue{std::numeric_limits<std::int64_t>::min()}));
    require_valid(validate_lua_value(extremes, LuaValue{std::numeric_limits<std::int64_t>::max()}));

    const Schema lowerOnly = Schema::integer(10, std::nullopt);
    require_valid(validate_lua_value(lowerOnly, LuaValue{std::numeric_limits<std::int64_t>::max()}));
    require_error(validate_lua_value(lowerOnly, LuaValue{9}), LuaSchemaErrorCode::Range, "");

    const Schema ratio = Schema::number(-1.5, 2.5);
    require_valid(validate_lua_value(ratio, LuaValue{-1.5}));
    require_valid(validate_lua_value(ratio, LuaValue{2.5}));
    require_error(validate_lua_value(ratio, LuaValue{2.5000001}), LuaSchemaErrorCode::Range, "");
    require_error(validate_lua_value(ratio, LuaValue{-1.5000001}), LuaSchemaErrorCode::Range, "");

    const Schema upperOnly = Schema::number(std::nullopt, 0.0);
    require_valid(validate_lua_value(upperOnly, LuaValue{-1e300}));
    require_error(validate_lua_value(upperOnly, LuaValue{0.5}), LuaSchemaErrorCode::Range, "");

    REQUIRE(level.integer_minimum() == std::optional<std::int64_t>{0});
    REQUIRE(level.integer_maximum() == std::optional<std::int64_t>{100});
    REQUIRE(!lowerOnly.integer_maximum().has_value());
    REQUIRE(ratio.number_minimum() == std::optional<double>{-1.5});
}

TEST_CASE("L0.1 string bounds count bytes and enums restrict values") {
    const Schema word = Schema::string(2, 4);
    require_valid(validate_lua_value(word, LuaValue{"ab"}));
    require_valid(validate_lua_value(word, LuaValue{"\xC3\xA9"}));
    require_valid(validate_lua_value(word, LuaValue{"abcd"}));
    require_error(validate_lua_value(word, LuaValue{"a"}), LuaSchemaErrorCode::Range, "");
    require_error(validate_lua_value(word, LuaValue{"abcde"}), LuaSchemaErrorCode::Range, "");

    const std::string withNul("o\0n", 3);
    const Schema mode = Schema::string(0, 8, std::vector<std::string>{"low", "high", withNul});
    require_valid(validate_lua_value(mode, LuaValue{"low"}));
    require_valid(validate_lua_value(mode, LuaValue{withNul}));
    require_error(validate_lua_value(mode, LuaValue{"mid"}), LuaSchemaErrorCode::Range, "");
    require_error(validate_lua_value(mode, LuaValue{"o"}), LuaSchemaErrorCode::Range, "");

    REQUIRE(mode.enumeration().has_value());
    REQUIRE(mode.enumeration()->size() == 3);
    REQUIRE(!word.enumeration().has_value());
    REQUIRE(word.minimum_bytes() == 2);
    REQUIRE(word.maximum_bytes() == 4);
}

TEST_CASE("L0.1 array bounds are inclusive and items are validated by index") {
    const Schema digits = Schema::array(Schema::integer(0, 9), 1, 3);
    require_valid(validate_lua_value(digits, LuaValue{LuaValue::Array{LuaValue{1}}}));
    require_valid(validate_lua_value(
        digits, LuaValue{LuaValue::Array{LuaValue{1}, LuaValue{2}, LuaValue{3}}}));
    require_error(validate_lua_value(digits, LuaValue{LuaValue::Array{}}), LuaSchemaErrorCode::Range, "");
    require_error(
        validate_lua_value(digits, LuaValue{LuaValue::Array{LuaValue{1}, LuaValue{2}, LuaValue{3}, LuaValue{4}}}),
        LuaSchemaErrorCode::Range, "");
    require_error(
        validate_lua_value(digits, LuaValue{LuaValue::Array{LuaValue{1}, LuaValue{"x"}}}),
        LuaSchemaErrorCode::Kind, "[2]");
    require_error(
        validate_lua_value(digits, LuaValue{LuaValue::Array{LuaValue{1}, LuaValue{10}}}),
        LuaSchemaErrorCode::Range, "[2]");

    REQUIRE(digits.item().kind() == LuaSchemaKind::Integer);
    REQUIRE(digits.minimum_items() == 1);
    REQUIRE(digits.maximum_items() == 3);
}

TEST_CASE("L0.1 invalid bounds are rejected at construction") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    REQUIRE_THROWS_AS(Schema::integer(5, 4), std::invalid_argument);
    REQUIRE_THROWS_AS(Schema::number(2.0, 1.0), std::invalid_argument);
    REQUIRE_THROWS_AS(Schema::number(nan, std::nullopt), std::invalid_argument);
    REQUIRE_THROWS_AS(Schema::number(std::nullopt, nan), std::invalid_argument);
    REQUIRE_THROWS_AS(Schema::number(std::nullopt, inf), std::invalid_argument);
    REQUIRE_THROWS_AS(Schema::number(-inf, std::nullopt), std::invalid_argument);
    REQUIRE_THROWS_AS(Schema::string(5, 4), std::invalid_argument);
    REQUIRE_THROWS_AS(Schema::array(Schema::boolean(), 3, 2), std::invalid_argument);

    REQUIRE_NOTHROW(Schema::integer(4, 4));
    REQUIRE_NOTHROW(Schema::number(1.0, 1.0));
    REQUIRE_NOTHROW(Schema::string(3, 3));
    REQUIRE_NOTHROW(Schema::array(Schema::boolean(), 2, 2));
}

TEST_CASE("L0.1 declared string bytes and array length at limit and limit+1") {
    REQUIRE_NOTHROW(Schema::string(0, LuaSchemaMaxStringBytes));
    REQUIRE_THROWS_AS(Schema::string(0, LuaSchemaMaxStringBytes + 1), std::invalid_argument);
    REQUIRE_NOTHROW(Schema::array(Schema::boolean(), 0, LuaSchemaMaxArrayItems));
    REQUIRE_THROWS_AS(
        Schema::array(Schema::boolean(), 0, LuaSchemaMaxArrayItems + 1), std::invalid_argument);
}

TEST_CASE("L0.1 invalid enums are rejected at construction") {
    REQUIRE_THROWS_AS(Schema::string(0, 8, std::vector<std::string>{}), std::invalid_argument);
    REQUIRE_THROWS_AS(
        Schema::string(0, 8, std::vector<std::string>{"low", "high", "low"}), std::invalid_argument);

    std::vector<std::string> entries;
    for (std::size_t index = 0; index < LuaSchemaMaxEnumEntries; ++index)
        entries.push_back(std::string(255, 'e') + static_cast<char>(index));
    const Schema atLimit = Schema::string(0, 256, entries);
    REQUIRE(atLimit.enum_bytes() == LuaSchemaMaxEnumBytes);

    std::vector<std::string> tooMany = entries;
    tooMany.push_back("extra");
    REQUIRE_THROWS_AS(Schema::string(0, 256, tooMany), std::invalid_argument);

    // Aggregate enum bytes are counted over the whole schema tree.
    std::vector<std::string> half;
    for (std::size_t index = 0; index < 128; ++index)
        half.push_back(std::string(255, 'h') + static_cast<char>(index));
    const Schema halfEnum = Schema::string(0, 256, half);
    REQUIRE_NOTHROW(Schema::object({{"a", halfEnum, true}, {"b", halfEnum, true}}));
    REQUIRE_THROWS_AS(
        Schema::object({{"a", halfEnum, true}, {"b", halfEnum, true}, {"c", Schema::string(0, 1, std::vector<std::string>{"x"}), true}}),
        std::invalid_argument);
}

TEST_CASE("L0.1 invalid object fields are rejected at construction") {
    REQUIRE_THROWS_AS(
        Schema::object({{"level", Schema::boolean(), true}, {"level", Schema::integer(0, 1), false}}),
        std::invalid_argument);

    REQUIRE(flat_object(LuaSchemaMaxFields).fields().size() == LuaSchemaMaxFields);
    REQUIRE_THROWS_AS(flat_object(LuaSchemaMaxFields + 1), std::invalid_argument);

    REQUIRE_NOTHROW(Schema::object({{std::string(LuaMaxNameBytes, 'k'), Schema::boolean(), true}}));
    REQUIRE_THROWS_AS(
        Schema::object({{std::string(LuaMaxNameBytes + 1, 'k'), Schema::boolean(), true}}),
        std::invalid_argument);

    // Field keys are arbitrary bytes, not Lua identifiers.
    const std::string oddKey("a b\0\xFF", 5);
    const Schema odd = Schema::object({{oddKey, Schema::boolean(), true}, {"end", Schema::boolean(), true}});
    require_valid(validate_lua_value(
        odd, LuaValue{LuaValue::Table{{oddKey, LuaValue{true}}, {"end", LuaValue{false}}}}));
}

TEST_CASE("L0.1 optional fields may be absent and unknown fields are always rejected") {
    const Schema light = Schema::object({
        {"level", Schema::integer(0, 100), true},
        {"on", Schema::boolean(), false}
    });

    require_valid(validate_lua_value(light, LuaValue{LuaValue::Table{{"level", LuaValue{1}}}}));
    require_valid(validate_lua_value(
        light, LuaValue{LuaValue::Table{{"level", LuaValue{1}}, {"on", LuaValue{true}}}}));
    require_error(
        validate_lua_value(light, LuaValue{LuaValue::Table{}}), LuaSchemaErrorCode::MissingField, ".level");
    require_error(
        validate_lua_value(light, LuaValue{LuaValue::Table{{"on", LuaValue{true}}}}),
        LuaSchemaErrorCode::MissingField, ".level");
    require_error(
        validate_lua_value(light, LuaValue{LuaValue::Table{{"level", LuaValue{1}}, {"zoom", LuaValue{1}}}}),
        LuaSchemaErrorCode::UnknownField, ".zoom");
    require_error(
        validate_lua_value(light, LuaValue{LuaValue::Table{{"level", LuaValue{1}}, {"on", LuaValue{1}}}}),
        LuaSchemaErrorCode::Kind, ".on");

    // An object without fields accepts only the empty table.
    require_valid(validate_lua_value(Schema::object({}), LuaValue{LuaValue::Table{}}));
    require_error(
        validate_lua_value(Schema::object({}), LuaValue{LuaValue::Table{{"x", LuaValue{1}}}}),
        LuaSchemaErrorCode::UnknownField, ".x");
}

TEST_CASE("L0.1 descriptions are validated and never alter validation") {
    const std::vector<std::string> invalid = {
        std::string("bad\xC3", 4),
        std::string("\xC0\xAF", 2),
        std::string("\xED\xA0\x80", 3),
        std::string("nul\0inside", 10),
        std::string(LuaMaxDescriptionBytes + 1, 'd')
    };

    for (const std::string& description : invalid) {
        INFO("description bytes " << description.size());
        REQUIRE_THROWS_AS(Schema::boolean(description), std::invalid_argument);
        REQUIRE_THROWS_AS(Schema::integer(0, 1, description), std::invalid_argument);
        REQUIRE_THROWS_AS(Schema::number(0.0, 1.0, description), std::invalid_argument);
        REQUIRE_THROWS_AS(Schema::string(0, 1, std::nullopt, description), std::invalid_argument);
        REQUIRE_THROWS_AS(Schema::array(Schema::boolean(), 0, 1, description), std::invalid_argument);
        REQUIRE_THROWS_AS(Schema::object({}, description), std::invalid_argument);
    }

    const Schema described = Schema::integer(0, 10, "Brightness in percent \xE2\x80\x94 0 is off");
    const Schema plain = Schema::integer(0, 10);
    REQUIRE(described.description() == "Brightness in percent \xE2\x80\x94 0 is off");
    REQUIRE_NOTHROW(Schema::boolean(std::string(LuaMaxDescriptionBytes, 'd')));

    for (const LuaValue& value : {LuaValue{5}, LuaValue{11}, LuaValue{5.0}}) {
        const LuaSchemaValidation withDescription = validate_lua_value(described, value);
        const LuaSchemaValidation without = validate_lua_value(plain, value);
        REQUIRE(withDescription.valid() == without.valid());
        if (withDescription.error)
            REQUIRE(withDescription.error->code == without.error->code);
    }
}

TEST_CASE("L0.1 container nesting at limit and limit+1 counts the root container") {
    REQUIRE(Schema::boolean().container_depth() == 0);
    REQUIRE(Schema::array(Schema::boolean(), 0, 1).container_depth() == 1);
    REQUIRE(Schema::object({{"a", Schema::array(Schema::boolean(), 0, 1), true}}).container_depth() == 2);

    const Schema atLimit = nested_arrays(LuaSchemaMaxContainerNesting);
    REQUIRE(atLimit.container_depth() == LuaSchemaMaxContainerNesting);
    require_valid(validate_lua_value(atLimit, nested_array_value(LuaSchemaMaxContainerNesting)));
    REQUIRE_THROWS_AS(Schema::array(atLimit, 0, 1), std::invalid_argument);
    REQUIRE_THROWS_AS(Schema::object({{"deep", atLimit, true}}), std::invalid_argument);
}

TEST_CASE("L0.1 expanded node count at limit and limit+1 counts shared copies") {
    REQUIRE(Schema::boolean().node_count() == 1);
    REQUIRE(flat_object(3).node_count() == 4);
    REQUIRE(Schema::array(Schema::boolean(), 0, 4096).node_count() == 2);

    const Schema block = flat_object(255);
    REQUIRE(block.node_count() == 256);

    std::vector<LuaSchemaField> fields;
    for (std::size_t index = 0; index < 15; ++index)
        fields.push_back({field_name(index), block, true});
    std::vector<LuaSchemaField> overflow = fields;

    fields.push_back({field_name(15), flat_object(254), true});
    const Schema atLimit = Schema::object(fields);
    REQUIRE(atLimit.node_count() == LuaSchemaMaxNodes);

    // The same shared block again: nodes are counted per expansion.
    overflow.push_back({field_name(15), block, true});
    REQUIRE_THROWS_AS(Schema::object(overflow), std::invalid_argument);
}

TEST_CASE("L0.1 first failure is deterministic in unsigned-byte field order and array index order") {
    const Schema unordered = Schema::object({
        {"z", Schema::boolean(), true},
        {"\xC3\xA9", Schema::boolean(), true},
        {"a", Schema::boolean(), true},
        {"B", Schema::boolean(), true}
    });
    const std::vector<LuaSchemaField>& fields = unordered.fields();
    REQUIRE(fields.size() == 4);
    REQUIRE(fields[0].name == "B");
    REQUIRE(fields[1].name == "a");
    REQUIRE(fields[2].name == "z");
    REQUIRE(fields[3].name == "\xC3\xA9");

    const LuaValue twoBad{LuaValue::Table{
        {"B", LuaValue{true}},
        {"a", LuaValue{true}},
        {"z", LuaValue{1}},
        {"\xC3\xA9", LuaValue{1}}
    }};
    for (int attempt = 0; attempt < 3; ++attempt)
        require_error(validate_lua_value(unordered, twoBad), LuaSchemaErrorCode::Kind, ".z");

    // An unknown key that sorts before a missing field is reported first.
    const Schema pair = Schema::object({{"b", Schema::boolean(), true}});
    require_error(
        validate_lua_value(pair, LuaValue{LuaValue::Table{{"a", LuaValue{true}}}}),
        LuaSchemaErrorCode::UnknownField, ".a");

    const Schema rooms = Schema::object({{
        "rooms",
        Schema::array(Schema::object({{"name", Schema::string(1, 8), true}}), 0, 4),
        true
    }});
    const LuaValue badRooms{LuaValue::Table{{"rooms", LuaValue{LuaValue::Array{
        LuaValue{LuaValue::Table{{"name", LuaValue{"hall"}}}},
        LuaValue{LuaValue::Table{{"name", LuaValue{""}}}},
        LuaValue{LuaValue::Table{{"name", LuaValue{2}}}}
    }}}}};
    require_error(validate_lua_value(rooms, badRooms), LuaSchemaErrorCode::Range, ".rooms[2].name");

    const Schema quoted = Schema::object({
        {"a b", Schema::boolean(), true},
        {"end", Schema::boolean(), true},
        {std::string("\x01", 1), Schema::boolean(), true}
    });
    require_error(
        validate_lua_value(quoted, LuaValue{LuaValue::Table{
            {std::string("\x01", 1), LuaValue{1}}, {"a b", LuaValue{true}}, {"end", LuaValue{true}}}}),
        LuaSchemaErrorCode::Kind, "[\"\\001\"]");
    require_error(
        validate_lua_value(quoted, LuaValue{LuaValue::Table{
            {std::string("\x01", 1), LuaValue{true}}, {"a b", LuaValue{1}}, {"end", LuaValue{true}}}}),
        LuaSchemaErrorCode::Kind, "[\"a b\"]");
    require_error(
        validate_lua_value(quoted, LuaValue{LuaValue::Table{
            {std::string("\x01", 1), LuaValue{true}}, {"a b", LuaValue{true}}, {"end", LuaValue{1}}}}),
        LuaSchemaErrorCode::Kind, "[\"end\"]");
}

TEST_CASE("L0.1 error path and diagnostic stay within 4096 bytes ending in ...") {
    const std::string key(LuaMaxNameBytes, '\x01');
    Schema schema = Schema::integer(0, 1);
    LuaValue value{std::int64_t{5}};
    for (std::size_t level = 0; level < LuaSchemaMaxContainerNesting; ++level) {
        schema = Schema::object({{key, schema, true}});
        value = LuaValue{LuaValue::Table{{key, value}}};
    }

    const LuaSchemaValidation validation = validate_lua_value(schema, value);
    REQUIRE(!validation.valid());
    REQUIRE(validation.error->code == LuaSchemaErrorCode::Range);
    const std::string& path = validation.error->path;
    REQUIRE(path.size() <= LuaMaxSchemaDiagnosticBytes);
    REQUIRE(path.size() >= 3);
    REQUIRE(path.substr(path.size() - 3) == "...");
    REQUIRE(path.rfind("[\"\\001\\001", 0) == 0);
    REQUIRE(validation.error->diagnostic.size() <= LuaMaxSchemaDiagnosticBytes);
}

TEST_CASE("L0.1 values beyond fixed L0 ceilings fail with Limit") {
    const Schema small = Schema::object({{"a", Schema::boolean(), false}});
    require_error(
        validate_lua_value(small, LuaValue{LuaValue::Table{{std::string(LuaMaxNameBytes + 1, 'k'), LuaValue{true}}}}),
        LuaSchemaErrorCode::Limit, "");
    require_error(
        validate_lua_value(small, LuaValue{LuaValue::Table{{std::string(LuaMaxNameBytes, 'k'), LuaValue{true}}}}),
        LuaSchemaErrorCode::UnknownField, "." + std::string(LuaMaxNameBytes, 'k'));

    const Schema grid = Schema::array(Schema::array(Schema::boolean(), 0, 4096), 0, 4096);
    auto row = [](std::size_t items) {
        return LuaValue{LuaValue::Array(items, LuaValue{true})};
    };

    // 1 root + 3 * (1 + 4096) + (1 + 4091) = 16,384 value nodes.
    LuaValue::Array atLimit{row(4096), row(4096), row(4096), row(4091)};
    require_valid(validate_lua_value(grid, LuaValue{atLimit}));

    LuaValue::Array overLimit{row(4096), row(4096), row(4096), row(4092)};
    const LuaSchemaValidation validation = validate_lua_value(grid, LuaValue{overLimit});
    REQUIRE(!validation.valid());
    REQUIRE(validation.error->code == LuaSchemaErrorCode::Limit);
}

TEST_CASE("L0.1 schemas are immutable values and copies share their tree") {
    static_assert(!std::is_default_constructible_v<LuaValueSchema>);
    static_assert(std::is_copy_constructible_v<LuaValueSchema>);
    static_assert(std::is_copy_assignable_v<LuaValueSchema>);

    const Schema original = Schema::object({{"level", Schema::integer(0, 100), true}}, "Light");
    const Schema copy = original;
    REQUIRE(copy.kind() == LuaSchemaKind::Object);
    REQUIRE(copy.description() == "Light");
    REQUIRE(&copy.fields() == &original.fields());
    REQUIRE(copy.node_count() == original.node_count());
    REQUIRE(copy.node_count() == 2);
    REQUIRE(copy.description_bytes() == 5);
}

TEST_CASE("L0.1 binding metadata requires both schemas and validates its description") {
    static_assert(!std::is_default_constructible_v<LuaModelBindingMetadata>);
    static_assert(!std::is_constructible_v<LuaModelBindingMetadata, LuaValueSchema>);
    static_assert(!std::is_constructible_v<LuaModelBindingMetadata, LuaValueSchema, std::string>);
    static_assert(std::is_constructible_v<LuaModelBindingMetadata, LuaValueSchema, LuaValueSchema>);

    const Schema level = Schema::object({{"level", Schema::integer(0, 100), true}});
    const LuaModelBindingMetadata symmetric = symmetric_metadata(level, "Brightness");
    REQUIRE(symmetric.description() == "Brightness");
    REQUIRE(symmetric.read_schema().kind() == LuaSchemaKind::Object);
    REQUIRE(symmetric.write_schema().kind() == LuaSchemaKind::Object);
    REQUIRE(symmetric.read_schema().fields().size() == 1);
    REQUIRE(symmetric.write_schema().fields().front().name == "level");

    const LuaModelBindingMetadata asymmetric(level, Schema::object({}));
    REQUIRE(asymmetric.write_schema().fields().empty());
    REQUIRE(asymmetric.description().empty());

    REQUIRE_THROWS_AS(symmetric_metadata(level, std::string("\xFF", 1)), std::invalid_argument);
    REQUIRE_THROWS_AS(symmetric_metadata(level, std::string("a\0b", 3)), std::invalid_argument);
    REQUIRE_THROWS_AS(
        symmetric_metadata(level, std::string(LuaMaxDescriptionBytes + 1, 'd')), std::invalid_argument);
    REQUIRE_NOTHROW(symmetric_metadata(level, std::string(LuaMaxDescriptionBytes, 'd')));
    REQUIRE_THROWS_AS(
        LuaModelBindingMetadata(level, level, std::string("\xC3", 1)), std::invalid_argument);
}
