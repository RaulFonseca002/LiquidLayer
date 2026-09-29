#include "liquid/scripting/LuaCapabilityManifest.hpp"
#include "liquid/world/World.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace liquid;
using namespace liquid::scripting;

namespace {

using Schema = LuaValueSchema;

template <int Tag>
struct Tagged {
    std::int64_t level = 0;
};

using Light = Tagged<0>;

template <int Tag>
ComponentCodec<Tagged<Tag>> tagged_codec() {
    return {
        [](const Tagged<Tag>& value) { return Value{value.level}; },
        [](const Value& value) { return Tagged<Tag>{value.as_signed_integer()}; }
    };
}

template <int Tag>
LuaComponentCodec<Tagged<Tag>> lua_tagged_codec() {
    return {
        [](const Tagged<Tag>& value) {
            return LuaValue{LuaValue::Table{{"level", LuaValue{value.level}}}};
        },
        [](const LuaValue& value) {
            return Tagged<Tag>{value.as_table().at("level").as_integer()};
        }
    };
}

template <int Tag>
LuaComponentCodec<Tagged<Tag>> counting_codec(std::size_t& decodes) {
    return {
        [](const Tagged<Tag>& value) {
            return LuaValue{LuaValue::Table{{"level", LuaValue{value.level}}}};
        },
        [&decodes](const LuaValue& value) {
            ++decodes;
            return Tagged<Tag>{value.as_table().at("level").as_integer()};
        }
    };
}

// 0 encodes normally; 1, 2 and 3 throw runtime_error, bad_alloc and a non-standard exception.
template <int Tag>
LuaComponentCodec<Tagged<Tag>> throwing_codec(const int& mode) {
    return {
        [&mode](const Tagged<Tag>& value) {
            if (mode == 1)
                throw std::runtime_error("snapshot failed");
            if (mode == 2)
                throw std::bad_alloc();
            if (mode == 3)
                throw 7;
            return LuaValue{LuaValue::Table{{"level", LuaValue{value.level}}}};
        },
        [](const LuaValue& value) {
            return Tagged<Tag>{value.as_table().at("level").as_integer()};
        }
    };
}

Schema level_schema() {
    return Schema::object({{"level", Schema::integer(0, 100), true}});
}

struct Lamp {
    std::int64_t level = 0;
    std::int64_t watts = 0;
};

ComponentCodec<Lamp> lamp_codec() {
    return {
        [](const Lamp& lamp) {
            return Value{Value::Object{{"level", Value{lamp.level}}, {"watts", Value{lamp.watts}}}};
        },
        [](const Value& value) {
            const Value::Object& object = value.as_object();
            return Lamp{object.at("level").as_signed_integer(), object.at("watts").as_signed_integer()};
        }
    };
}

// Reads level and watts; writes level only.
LuaComponentCodec<Lamp> lua_lamp_codec(std::size_t& decodes) {
    return {
        [](const Lamp& lamp) {
            return LuaValue{LuaValue::Table{{"level", LuaValue{lamp.level}}, {"watts", LuaValue{lamp.watts}}}};
        },
        [&decodes](const LuaValue& value) {
            ++decodes;
            return Lamp{value.as_table().at("level").as_integer(), 0};
        }
    };
}

Schema lamp_read_schema() {
    return Schema::object({
        {"level", Schema::integer(0, 100), true},
        {"watts", Schema::integer(0, 1000), true}
    });
}

Schema lamp_write_schema() {
    return level_schema();
}

struct Samples {
    std::vector<std::int64_t> values;
};

ComponentCodec<Samples> samples_codec() {
    return {
        [](const Samples& samples) {
            Value::Array array;
            for (std::int64_t value : samples.values)
                array.push_back(Value{value});
            return Value{std::move(array)};
        },
        [](const Value& value) {
            Samples samples;
            for (const Value& item : value.as_array())
                samples.values.push_back(item.as_signed_integer());
            return samples;
        }
    };
}

LuaComponentCodec<Samples> lua_samples_codec(std::size_t& decodes) {
    return {
        [](const Samples& samples) {
            LuaValue::Array array;
            for (std::int64_t value : samples.values)
                array.push_back(LuaValue{value});
            return LuaValue{std::move(array)};
        },
        [&decodes](const LuaValue& value) {
            ++decodes;
            Samples samples;
            for (const LuaValue& item : value.as_array())
                samples.values.push_back(item.as_integer());
            return samples;
        }
    };
}

Schema samples_schema() {
    return Schema::array(Schema::integer(std::nullopt, std::nullopt), 0, 4096);
}

struct Settings {
    std::optional<bool> on;
};

ComponentCodec<Settings> settings_codec() {
    return {
        [](const Settings& settings) { return Value{static_cast<std::int64_t>(settings.on.value_or(false))}; },
        [](const Value& value) { return Settings{value.as_signed_integer() != 0}; }
    };
}

LuaComponentCodec<Settings> lua_settings_codec() {
    return {
        [](const Settings& settings) {
            LuaValue::Table table;
            if (settings.on)
                table.emplace("on", LuaValue{*settings.on});
            return LuaValue{std::move(table)};
        },
        [](const LuaValue& value) {
            Settings settings;
            const LuaValue::Table& table = value.as_table();
            if (const auto found = table.find("on"); found != table.end())
                settings.on = found->second.as_bool();
            return settings;
        }
    };
}

struct Text {
    std::string text;
};

ComponentCodec<Text> text_codec() {
    return {
        [](const Text& text) { return Value{text.text}; },
        [](const Value& value) { return Text{value.as_string()}; }
    };
}

LuaComponentCodec<Text> lua_text_codec() {
    return {
        [](const Text& text) { return LuaValue{text.text}; },
        [](const LuaValue& value) { return Text{value.as_string()}; }
    };
}

std::string component_name(std::size_t index) {
    std::string digits = std::to_string(index);
    return "c" + std::string(4 - digits.size(), '0') + digits;
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char byte) {
        return static_cast<char>(std::tolower(byte));
    });
    return text;
}

void require_status(const LuaExecutionResult& result, LuaExecutionStatus status) {
    INFO(result.diagnostic);
    REQUIRE(result.status == status);
}

void require_manifest_error(const LuaManifestResult& result, LuaManifestErrorCode code) {
    REQUIRE(!result.ok());
    REQUIRE(result.error().code == code);
    REQUIRE(!result.error().diagnostic.empty());
    REQUIRE(result.error().diagnostic.size() <= LuaMaxManifestDiagnosticBytes);
    REQUIRE_THROWS_AS(result.manifest(), std::logic_error);
}

const LuaCapabilityManifest& require_manifest(const LuaManifestResult& result) {
    INFO((result.ok() ? std::string("manifest captured") : result.error().diagnostic));
    REQUIRE(result.ok());
    REQUIRE_THROWS_AS(result.error(), std::logic_error);
    return result.manifest();
}

void require_same_limits(const LuaExecutionLimits& left, const LuaExecutionLimits& right) {
    REQUIRE(left.maxSourceBytes == right.maxSourceBytes);
    REQUIRE(left.maxMemoryBytes == right.maxMemoryBytes);
    REQUIRE(left.maxInstructions == right.maxInstructions);
    REQUIRE(left.maxDiagnosticBytes == right.maxDiagnosticBytes);
    REQUIRE(left.maxCreatedIntents == right.maxCreatedIntents);
    REQUIRE(left.maxCancelledIntents == right.maxCancelledIntents);
    REQUIRE(left.maxWatches == right.maxWatches);
    REQUIRE(left.maxTableDepth == right.maxTableDepth);
    REQUIRE(left.maxTableEntries == right.maxTableEntries);
    REQUIRE(left.maxStringBytes == right.maxStringBytes);
    REQUIRE(left.maxBufferedValueBytes == right.maxBufferedValueBytes);
    REQUIRE(left.recordFullSource == right.recordFullSource);
}

void require_same_manifest(const LuaCapabilityManifest& left, const LuaCapabilityManifest& right) {
    REQUIRE(left.authoring_contract() == right.authoring_contract());
    REQUIRE(left.now_ms() == right.now_ms());
    REQUIRE(left.notes() == right.notes());
    require_same_limits(left.limits(), right.limits());
    REQUIRE(left.capabilities().size() == right.capabilities().size());
    for (std::size_t index = 0; index < left.capabilities().size(); ++index) {
        const LuaManifestCapability& a = left.capabilities()[index];
        const LuaManifestCapability& b = right.capabilities()[index];
        REQUIRE(a.scriptTypeName == b.scriptTypeName);
        REQUIRE(a.componentName == b.componentName);
        REQUIRE(a.accessExpression == b.accessExpression);
        REQUIRE(a.mode == b.mode);
        REQUIRE(a.description == b.description);
        REQUIRE(a.readValue == b.readValue);
        REQUIRE(a.readSchema.has_value() == b.readSchema.has_value());
        REQUIRE(a.writeSchema.has_value() == b.writeSchema.has_value());
    }
    REQUIRE(left.capture().entries().size() == right.capture().entries().size());
}

bool has_intent_on(World& world, ComponentTypeId type, ComponentSlotId slot, IntentId intent) {
    const std::vector<IntentId> intents = world.intents_for(type, slot);
    return std::find(intents.begin(), intents.end(), intent) != intents.end();
}

}

// L0.2 Four-argument binding overload and opt-in enforcement

TEST_CASE("L0.2 symmetric brightness binding accepts conforming proposals") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    world.add_component(lightType, "office", Light{10});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::ReadWrite);

    std::size_t decodes = 0;
    LuaBehaviorRunner runner;
    runner.expose_component(lightType, "Light", counting_codec<0>(decodes),
        symmetric_metadata(level_schema(), "Office brightness"));

    const LuaExecutionResult result = runner.execute(world, behavior, 0, R"lua(
        assert(access.Light.office.value.level == 10)
        access.Light.office.propose{name = "raise", value = {level = 70}, priority = "high"}
    )lua");
    require_status(result, LuaExecutionStatus::Success);
    REQUIRE(result.createdIntents.size() == 1);
    REQUIRE(decodes == 1);
    REQUIRE(world.typed_intent(lightType, result.createdIntents.front()).value.level == 70);
}

TEST_CASE("L0.2 write-schema failure is InvalidProposal before the decoder runs") {
    const std::vector<std::string> proposals = {
        "access.Light.office.propose{name = 'bad', value = {level = 150}}",
        "access.Light.office.propose{name = 'bad', value = {level = 70.5}}",
        "access.Light.office.propose{name = 'bad', value = {level = 70, extra = 1}}",
        "access.Light.office.propose{name = 'bad', value = {}}"
    };

    for (const std::string& source : proposals) {
        INFO(source);
        World world;
        const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
        world.add_component(lightType, "office", Light{10});
        const BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::ReadWrite);

        std::size_t decodes = 0;
        LuaBehaviorRunner runner;
        runner.expose_component(lightType, "Light", counting_codec<0>(decodes), symmetric_metadata(level_schema()));

        const LuaExecutionResult result = runner.execute(world, behavior, 0, source);
        require_status(result, LuaExecutionStatus::InvalidProposal);
        REQUIRE(decodes == 0);
        REQUIRE(result.createdIntents.empty());
        REQUIRE(world.intent_count(behavior) == 0);
    }
}

TEST_CASE("L0.2 read-schema failure on a readable snapshot is HostError before Lua runs") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    world.add_component(lightType, "office", Light{150});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::ReadWrite);

    LuaBehaviorRunner runner;
    runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema()));

    const LuaExecutionResult result = runner.execute(world, behavior, 0,
        "access.Light.office.propose{name = 'ran', value = {level = 50}}");
    require_status(result, LuaExecutionStatus::HostError);
    REQUIRE(result.createdIntents.empty());
    REQUIRE(world.intent_count(behavior) == 0);
}

TEST_CASE("L0.2 asymmetric read and write schemas are enforced per direction") {
    World world;
    const auto lampType = world.register_component<Lamp>("example.Lamp", 1, lamp_codec());
    world.add_component(lampType, "desk", Lamp{10, 60});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(lampType, behavior, "desk", ComponentAccessMode::ReadWrite);

    std::size_t decodes = 0;
    LuaBehaviorRunner runner;
    runner.expose_component(lampType, "Lamp", lua_lamp_codec(decodes),
        LuaModelBindingMetadata(lamp_read_schema(), lamp_write_schema(), "Desk lamp"));

    const LuaExecutionResult dim = runner.execute(world, behavior, 0, R"lua(
        assert(access.Lamp.desk.value.level == 10)
        assert(access.Lamp.desk.value.watts == 60)
        access.Lamp.desk.propose{name = "dim", value = {level = 5}}
    )lua");
    require_status(dim, LuaExecutionStatus::Success);
    REQUIRE(dim.createdIntents.size() == 1);
    REQUIRE(decodes == 1);

    const LuaExecutionResult watts = runner.execute(world, behavior, 0,
        "access.Lamp.desk.propose{name = 'watts', value = {level = 5, watts = 10}}");
    require_status(watts, LuaExecutionStatus::InvalidProposal);
    REQUIRE(decodes == 1);
    REQUIRE(world.intent_count(behavior) == 1);
}

TEST_CASE("L0.2 swapped schema direction is rejected at the matching boundary") {
    World world;
    const auto lampType = world.register_component<Lamp>("example.Lamp", 1, lamp_codec());
    world.add_component(lampType, "reader", Lamp{10, 60});
    world.add_component(lampType, "writer", Lamp{10, 60});
    const BehaviorId reader = world.create_behavior();
    const BehaviorId writer = world.create_behavior();
    world.grant_component_access(lampType, reader, "reader", ComponentAccessMode::Read);
    world.grant_component_access(lampType, writer, "writer", ComponentAccessMode::Write);

    std::size_t decodes = 0;
    LuaBehaviorRunner runner;
    // Swapped: the read schema lacks watts and the write schema requires it.
    runner.expose_component(lampType, "Lamp", lua_lamp_codec(decodes),
        LuaModelBindingMetadata(lamp_write_schema(), lamp_read_schema()));

    require_status(runner.execute(world, reader, 0, "return"), LuaExecutionStatus::HostError);

    const LuaExecutionResult write = runner.execute(world, writer, 0,
        "access.Lamp.writer.propose{name = 'dim', value = {level = 5}}");
    require_status(write, LuaExecutionStatus::InvalidProposal);
    REQUIRE(decodes == 0);
    REQUIRE(world.intent_count(writer) == 0);
}

TEST_CASE("L0.2 three-argument overload keeps schema-less behavior") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    world.add_component(lightType, "office", Light{150});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::ReadWrite);

    LuaBehaviorRunner runner;
    runner.expose_component(lightType, "Light", lua_tagged_codec<0>());

    const LuaExecutionResult result = runner.execute(world, behavior, 0, R"lua(
        assert(access.Light.office.value.level == 150)
        access.Light.office.propose{name = "any", value = {level = 999}}
    )lua");
    require_status(result, LuaExecutionStatus::Success);
    REQUIRE(result.createdIntents.size() == 1);
}

TEST_CASE("L0.2 metadata that cannot fit the runner limits is rejected and leaves no binding") {
    struct Case {
        const char* label;
        LuaExecutionLimits limits;
        Schema schema;
    };

    LuaExecutionLimits depth;
    depth.maxTableDepth = 2;
    LuaExecutionLimits strings;
    strings.maxStringBytes = 8;
    LuaExecutionLimits entries;
    entries.maxTableEntries = 5;
    LuaExecutionLimits buffered;
    buffered.maxBufferedValueBytes = 2 * sizeof(LuaValue) + 1 + 8 - 1;
    LuaExecutionLimits unbounded;
    unbounded.maxTableEntries = std::numeric_limits<std::size_t>::max();
    unbounded.maxStringBytes = std::numeric_limits<std::size_t>::max();
    unbounded.maxBufferedValueBytes = std::numeric_limits<std::size_t>::max();

    Schema overflowing = Schema::string(0, LuaSchemaMaxStringBytes);
    for (std::size_t level = 0; level < LuaSchemaMaxContainerNesting; ++level)
        overflowing = Schema::array(overflowing, 0, LuaSchemaMaxArrayItems);

    const std::vector<Case> cases = {
        {"depth", depth, Schema::object({{"a", Schema::object({{"b", Schema::object({}), true}}), true}})},
        {"string bytes", strings, Schema::object({{"s", Schema::string(0, 9), true}})},
        {"key bytes", strings, Schema::object({{"abcdefghi", Schema::boolean(), true}})},
        {"entries", entries, Schema::object({{"a", Schema::array(Schema::boolean(), 0, 5), true}})},
        {"buffered bytes", buffered, Schema::object({{"s", Schema::string(0, 8), true}})},
        {"checked overflow", unbounded, overflowing}
    };

    for (const Case& limitCase : cases) {
        INFO(limitCase.label);
        World world;
        const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
        world.add_component(lightType, "office", Light{150});
        const BehaviorId behavior = world.create_behavior();
        // A granted component makes a registered binding observable as access.Light.
        world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::Read);

        {
            LuaBehaviorRunner runner(limitCase.limits);
            REQUIRE_THROWS_AS(
                runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(limitCase.schema)),
                std::invalid_argument);
            require_status(runner.execute(world, behavior, 0, "assert(access.Light == nil)"), LuaExecutionStatus::Success);
        }

        LuaBehaviorRunner runner(limitCase.limits);
        REQUIRE_THROWS_AS(
            runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(limitCase.schema)),
            std::invalid_argument);
        REQUIRE_NOTHROW(runner.expose_component(lightType, "Light", lua_tagged_codec<0>()));
        require_status(runner.execute(world, behavior, 0, "assert(access.Light ~= nil)"), LuaExecutionStatus::Success);
    }
}

TEST_CASE("L0.2 metadata exactly at the runner limits is accepted") {
    LuaExecutionLimits depth;
    depth.maxTableDepth = 2;
    LuaExecutionLimits strings;
    strings.maxStringBytes = 8;
    LuaExecutionLimits entries;
    entries.maxTableEntries = 5;
    LuaExecutionLimits buffered;
    buffered.maxBufferedValueBytes = 2 * sizeof(LuaValue) + 1 + 8;

    const std::vector<std::pair<LuaExecutionLimits, Schema>> cases = {
        {depth, Schema::object({{"a", Schema::object({}), true}})},
        {strings, Schema::object({{"abcdefgh", Schema::string(0, 8), true}})},
        {entries, Schema::object({{"a", Schema::array(Schema::boolean(), 0, 4), true}})},
        {buffered, Schema::object({{"s", Schema::string(0, 8), true}})}
    };

    for (const auto& [limits, schema] : cases) {
        World world;
        const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
        LuaBehaviorRunner runner(limits);
        REQUIRE_NOTHROW(runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(schema)));
    }
}

TEST_CASE("L0.2 invalid four-argument registrations leave no binding") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    const auto otherType = world.register_component<Tagged<1>>("example.Other", 1, tagged_codec<1>());
    const BehaviorId behavior = world.create_behavior();

    LuaBehaviorRunner runner;
    REQUIRE_THROWS_AS(
        runner.expose_component(lightType, std::string(LuaMaxNameBytes + 1, 'L'), lua_tagged_codec<0>(),
            symmetric_metadata(level_schema())),
        std::invalid_argument);
    REQUIRE_THROWS_AS(
        runner.expose_component(lightType, std::string("Li\0ght", 6), lua_tagged_codec<0>(),
            symmetric_metadata(level_schema())),
        std::invalid_argument);
    REQUIRE_THROWS_AS(
        runner.expose_component(lightType, "", lua_tagged_codec<0>(), symmetric_metadata(level_schema())),
        std::invalid_argument);
    REQUIRE_THROWS_AS(
        runner.expose_component(lightType, "Light", LuaComponentCodec<Light>{}, symmetric_metadata(level_schema())),
        std::invalid_argument);

    REQUIRE_NOTHROW(runner.expose_component(lightType, std::string(LuaMaxNameBytes, 'L'), lua_tagged_codec<0>(),
        symmetric_metadata(level_schema())));
    REQUIRE_THROWS_AS(
        runner.expose_component(otherType, std::string(LuaMaxNameBytes, 'L'), lua_tagged_codec<1>(),
            symmetric_metadata(level_schema())),
        std::runtime_error);
    REQUIRE_NOTHROW(runner.expose_component(otherType, "Light", lua_tagged_codec<1>(), symmetric_metadata(level_schema())));

    require_status(runner.execute(world, behavior, 0, "return"), LuaExecutionStatus::Success);
    const auto thirdType = world.register_component<Tagged<2>>("example.Third", 1, tagged_codec<2>());
    REQUIRE_THROWS_AS(
        runner.expose_component(thirdType, "Third", lua_tagged_codec<2>(), symmetric_metadata(level_schema())),
        std::logic_error);
}

// L0.3 Behavior manifest and freeze-on-success

TEST_CASE("L0.3 manifest projects Read, Write and ReadWrite permissions") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    world.add_component(lightType, "attic", Light{1});
    world.add_component(lightType, "hall", Light{2});
    world.add_component(lightType, "office", Light{3});
    world.add_component(lightType, "hidden", Light{4});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(lightType, behavior, "attic", ComponentAccessMode::Read);
    world.grant_component_access(lightType, behavior, "hall", ComponentAccessMode::Write);
    world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::ReadWrite);

    LuaBehaviorRunner runner;
    runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema(), "Brightness"));

    const LuaManifestResult result = runner.capability_manifest(world, behavior, 1'000);
    const LuaCapabilityManifest& manifest = require_manifest(result);
    const std::vector<LuaManifestCapability>& capabilities = manifest.capabilities();
    REQUIRE(capabilities.size() == 3);

    const LuaManifestCapability& attic = capabilities[0];
    REQUIRE(attic.scriptTypeName == "Light");
    REQUIRE(attic.componentName == "attic");
    REQUIRE(attic.mode == ComponentAccessMode::Read);
    REQUIRE(attic.description == "Brightness");
    REQUIRE(attic.readSchema.has_value());
    REQUIRE(attic.readValue == std::optional<LuaValue>{LuaValue{LuaValue::Table{{"level", LuaValue{1}}}}});
    REQUIRE(!attic.writeSchema.has_value());

    const LuaManifestCapability& hall = capabilities[1];
    REQUIRE(hall.componentName == "hall");
    REQUIRE(hall.mode == ComponentAccessMode::Write);
    REQUIRE(!hall.readSchema.has_value());
    REQUIRE(!hall.readValue.has_value());
    REQUIRE(hall.writeSchema.has_value());
    REQUIRE(hall.writeSchema->fields().front().name == "level");

    const LuaManifestCapability& office = capabilities[2];
    REQUIRE(office.componentName == "office");
    REQUIRE(office.mode == ComponentAccessMode::ReadWrite);
    REQUIRE(office.readSchema.has_value());
    REQUIRE(office.readValue == std::optional<LuaValue>{LuaValue{LuaValue::Table{{"level", LuaValue{3}}}}});
    REQUIRE(office.writeSchema.has_value());

    // Discovery neither executes Lua nor creates intents.
    REQUIRE(world.intent_count(behavior) == 0);
}

TEST_CASE("L0.3 write-only capabilities never take a snapshot") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    world.add_component(lightType, "hall", Light{2});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(lightType, behavior, "hall", ComponentAccessMode::Write);

    int mode = 1;
    LuaBehaviorRunner runner;
    runner.expose_component(lightType, "Light", throwing_codec<0>(mode), symmetric_metadata(level_schema()));

    const LuaManifestResult captured = runner.capability_manifest(world, behavior, 0);
    const LuaCapabilityManifest& manifest = require_manifest(captured);
    REQUIRE(manifest.capabilities().size() == 1);
    REQUIRE(!manifest.capabilities().front().readValue.has_value());
}

TEST_CASE("L0.3 schema-less bindings are omitted from the manifest") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    const auto plainType = world.register_component<Tagged<1>>("example.Plain", 1, tagged_codec<1>());
    world.add_component(lightType, "office", Light{3});
    world.add_component(plainType, "office", Tagged<1>{4});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::ReadWrite);
    world.grant_component_access(plainType, behavior, "office", ComponentAccessMode::ReadWrite);

    LuaBehaviorRunner runner;
    runner.expose_component(plainType, "Plain", lua_tagged_codec<1>());
    runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema()));

    const LuaManifestResult captured = runner.capability_manifest(world, behavior, 0);
    const LuaCapabilityManifest& manifest = require_manifest(captured);
    REQUIRE(manifest.capabilities().size() == 1);
    REQUIRE(manifest.capabilities().front().scriptTypeName == "Light");
    REQUIRE(manifest.capture().entries().size() == 1);
    REQUIRE(manifest.capture().entries().front().bindingIndex == 1);
}

TEST_CASE("L0.3 manifest carries the contract, host time, effective limits and authoring notes") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    world.add_component(lightType, "office", Light{3});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::Read);

    LuaExecutionLimits limits;
    limits.maxInstructions = 12'345;
    limits.maxTableDepth = 8;
    limits.maxCreatedIntents = 3;
    limits.recordFullSource = false;
    LuaBehaviorRunner runner(limits);
    runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema()));

    const IntentTime now = static_cast<IntentTime>(std::numeric_limits<std::int64_t>::max());
    const LuaManifestResult captured = runner.capability_manifest(world, behavior, now);
    const LuaCapabilityManifest& manifest = require_manifest(captured);
    REQUIRE(manifest.authoring_contract() == "liquid.lua.authoring/1");
    REQUIRE(manifest.authoring_contract() == LuaAuthoringContract);
    REQUIRE(manifest.now_ms() == now);
    require_same_limits(manifest.limits(), limits);
    require_same_limits(manifest.capture().limits(), limits);

    std::string notes;
    for (const std::string& note : manifest.notes())
        notes += lowercase(note) + "\n";
    for (const char* keyword : {"fresh", "owner", "time", "named", "persistent", "until",
             "physical", "integer", "float", "{}", "object", "empty array"}) {
        INFO(keyword);
        REQUIRE(notes.find(keyword) != std::string::npos);
    }
}

TEST_CASE("L0.3 host time outside the Lua integer range is HostError") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    const BehaviorId behavior = world.create_behavior();
    LuaBehaviorRunner runner;
    runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema()));

    const IntentTime tooLate = static_cast<IntentTime>(std::numeric_limits<std::int64_t>::max()) + 1;
    require_manifest_error(runner.capability_manifest(world, behavior, tooLate), LuaManifestErrorCode::HostError);
    REQUIRE_NOTHROW(runner.expose_component(
        world.register_component<Tagged<1>>("example.Other", 1, tagged_codec<1>()), "Other", lua_tagged_codec<1>()));
}

TEST_CASE("L0.3 snapshot and allocation failures are SnapshotUnavailable with no partial manifest") {
    for (int failure : {1, 2, 3}) {
        INFO("failure mode " << failure);
        World world;
        const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
        const auto goodType = world.register_component<Tagged<1>>("example.Good", 1, tagged_codec<1>());
        world.add_component(lightType, "office", Light{3});
        world.add_component(goodType, "office", Tagged<1>{4});
        const BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::Read);
        world.grant_component_access(goodType, behavior, "office", ComponentAccessMode::Read);

        int mode = failure;
        LuaBehaviorRunner runner;
        // "Alpha" sorts first, so a partial manifest would already hold it.
        runner.expose_component(goodType, "Alpha", lua_tagged_codec<1>(), symmetric_metadata(level_schema()));
        runner.expose_component(lightType, "Light", throwing_codec<0>(mode), symmetric_metadata(level_schema()));

        require_manifest_error(runner.capability_manifest(world, behavior, 0), LuaManifestErrorCode::SnapshotUnavailable);
    }
}

TEST_CASE("L0.3 codec value that violates the read schema is SchemaMismatch") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    world.add_component(lightType, "office", Light{150});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::Read);

    LuaBehaviorRunner runner;
    runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema()));
    require_manifest_error(runner.capability_manifest(world, behavior, 0), LuaManifestErrorCode::SchemaMismatch);
}

TEST_CASE("L0.3 unknown and destroyed behaviors are InvalidBehavior") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    const BehaviorId destroyed = world.create_behavior();
    world.destroy_behavior(destroyed);

    LuaBehaviorRunner runner;
    runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema()));
    require_manifest_error(runner.capability_manifest(world, BehaviorId{}, 0), LuaManifestErrorCode::InvalidBehavior);
    require_manifest_error(runner.capability_manifest(world, destroyed, 0), LuaManifestErrorCode::InvalidBehavior);
}

TEST_CASE("L0.3 manifest values are copies that outlive component, runner and world") {
    auto world = std::make_unique<World>();
    const auto lightType = world->register_component<Light>("example.Light", 1, tagged_codec<0>());
    world->add_component(lightType, "office", Light{42});
    const BehaviorId behavior = world->create_behavior();
    world->grant_component_access(lightType, behavior, "office", ComponentAccessMode::Read);

    auto runner = std::make_unique<LuaBehaviorRunner>();
    runner->expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema(), "Kept"));

    std::optional<LuaManifestResult> result;
    result.emplace(runner->capability_manifest(*world, behavior, 0));
    REQUIRE(result->ok());

    world->remove_component(lightType, "office");
    runner.reset();
    world.reset();

    const LuaManifestCapability& capability = result->manifest().capabilities().at(0);
    REQUIRE(capability.readValue->as_table().at("level").as_integer() == 42);
    REQUIRE(capability.description == "Kept");
    REQUIRE(capability.readSchema->fields().front().name == "level");
}

TEST_CASE("L0.3 order is unsigned-byte lexical and repeated captures are identical") {
    World world;
    const auto zetaType = world.register_component<Tagged<1>>("example.Zeta", 1, tagged_codec<1>());
    const auto alphaType = world.register_component<Tagged<2>>("example.Alpha", 1, tagged_codec<2>());
    const auto accentType = world.register_component<Tagged<3>>("example.Accent", 1, tagged_codec<3>());
    const BehaviorId behavior = world.create_behavior();
    for (const char* name : {"office", "Attic", "\xC3\xA9tage"}) {
        world.add_component(zetaType, name, Tagged<1>{1});
        world.add_component(alphaType, name, Tagged<2>{2});
        world.add_component(accentType, name, Tagged<3>{3});
        world.grant_component_access(zetaType, behavior, name, ComponentAccessMode::Read);
        world.grant_component_access(alphaType, behavior, name, ComponentAccessMode::Read);
        world.grant_component_access(accentType, behavior, name, ComponentAccessMode::Read);
    }

    LuaBehaviorRunner runner;
    runner.expose_component(zetaType, "Zeta", lua_tagged_codec<1>(), symmetric_metadata(level_schema()));
    runner.expose_component(alphaType, "Alpha", lua_tagged_codec<2>(), symmetric_metadata(level_schema()));
    runner.expose_component(accentType, "\xC3\xA9", lua_tagged_codec<3>(), symmetric_metadata(level_schema()));

    const LuaManifestResult first = runner.capability_manifest(world, behavior, 7);
    const LuaManifestResult second = runner.capability_manifest(world, behavior, 7);
    const LuaCapabilityManifest& manifest = require_manifest(first);
    require_same_manifest(manifest, require_manifest(second));

    const std::vector<std::pair<std::string, std::string>> expected = {
        {"Alpha", "Attic"}, {"Alpha", "office"}, {"Alpha", "\xC3\xA9tage"},
        {"Zeta", "Attic"}, {"Zeta", "office"}, {"Zeta", "\xC3\xA9tage"},
        {"\xC3\xA9", "Attic"}, {"\xC3\xA9", "office"}, {"\xC3\xA9", "\xC3\xA9tage"}
    };
    REQUIRE(manifest.capabilities().size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        REQUIRE(manifest.capabilities()[index].scriptTypeName == expected[index].first);
        REQUIRE(manifest.capabilities()[index].componentName == expected[index].second);
    }

    const std::vector<LuaManifestCapture::Entry>& entries = manifest.capture().entries();
    REQUIRE(entries.size() == expected.size());
    REQUIRE(entries[0].bindingIndex == 1);
    REQUIRE(entries[0].type == alphaType.id);
    REQUIRE(entries[3].bindingIndex == 0);
    REQUIRE(entries[3].type == zetaType.id);
    REQUIRE(entries[6].bindingIndex == 2);
    REQUIRE(entries[6].type == accentType.id);
    REQUIRE(entries[1].slot == world.get_components(alphaType, behavior).at("office"));
}

TEST_CASE("L0.3 private capture data identifies world, behavior, revision and runner") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    world.add_component(lightType, "office", Light{3});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::Read);

    LuaBehaviorRunner firstRunner;
    LuaBehaviorRunner secondRunner;
    firstRunner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema()));
    secondRunner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema()));

    const LuaManifestResult first = firstRunner.capability_manifest(world, behavior, 0);
    const LuaManifestResult second = secondRunner.capability_manifest(world, behavior, 0);
    const LuaManifestCapture& capture = require_manifest(first).capture();
    REQUIRE(capture.world_instance() == world.instance_id());
    REQUIRE(capture.behavior() == behavior);
    REQUIRE(capture.access_revision() == world.behavior_access_revision(behavior));
    REQUIRE(capture.runner_id() != 0);
    REQUIRE(require_manifest(second).capture().runner_id() != 0);
    REQUIRE(require_manifest(second).capture().runner_id() != capture.runner_id());
    REQUIRE(capture.entries().size() == 1);
    REQUIRE(capture.entries().front().bindingIndex == 0);
    REQUIRE(capture.entries().front().type == lightType.id);
    REQUIRE(capture.entries().front().slot == world.get_components(lightType, behavior).at("office"));
}

TEST_CASE("L0.3 successful capture freezes registration and failed capture allows retry") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    const auto otherType = world.register_component<Tagged<1>>("example.Other", 1, tagged_codec<1>());
    const auto lateType = world.register_component<Tagged<2>>("example.Late", 1, tagged_codec<2>());
    world.add_component(lightType, "office", Light{3});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::Read);

    int mode = 1;
    LuaBehaviorRunner runner;
    runner.expose_component(lightType, "Light", throwing_codec<0>(mode), symmetric_metadata(level_schema()));

    require_manifest_error(runner.capability_manifest(world, behavior, 0), LuaManifestErrorCode::SnapshotUnavailable);
    REQUIRE_NOTHROW(runner.expose_component(otherType, "Other", lua_tagged_codec<1>(), symmetric_metadata(level_schema())));

    mode = 0;
    require_manifest(runner.capability_manifest(world, behavior, 0));
    REQUIRE_THROWS_AS(runner.expose_component(lateType, "Late", lua_tagged_codec<2>()), std::logic_error);
    REQUIRE_THROWS_AS(
        runner.expose_component(lateType, "Late", lua_tagged_codec<2>(), symmetric_metadata(level_schema())),
        std::logic_error);
}

// L0.4 Expressions, limits and installed surface

TEST_CASE("L0.4 generated expressions address keyword, punctuation, escape and non-ASCII names in real Lua") {
    World world;
    const auto t0 = world.register_component<Tagged<0>>("example.T0", 1, tagged_codec<0>());
    const auto t1 = world.register_component<Tagged<1>>("example.T1", 1, tagged_codec<1>());
    const auto t2 = world.register_component<Tagged<2>>("example.T2", 1, tagged_codec<2>());
    const auto t3 = world.register_component<Tagged<3>>("example.T3", 1, tagged_codec<3>());
    const auto t4 = world.register_component<Tagged<4>>("example.T4", 1, tagged_codec<4>());
    const std::string controlName("\x01\x7F", 2);
    world.add_component(t0, "office", Tagged<0>{1});
    world.add_component(t1, "a-b", Tagged<1>{1});
    world.add_component(t2, "sp ace", Tagged<2>{1});
    world.add_component(t3, controlName, Tagged<3>{1});
    world.add_component(t4, "1st", Tagged<4>{1});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(t0, behavior, "office", ComponentAccessMode::ReadWrite);
    world.grant_component_access(t1, behavior, "a-b", ComponentAccessMode::ReadWrite);
    world.grant_component_access(t2, behavior, "sp ace", ComponentAccessMode::ReadWrite);
    world.grant_component_access(t3, behavior, controlName, ComponentAccessMode::ReadWrite);
    world.grant_component_access(t4, behavior, "1st", ComponentAccessMode::ReadWrite);

    LuaBehaviorRunner runner;
    runner.expose_component(t0, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema()));
    runner.expose_component(t1, "end", lua_tagged_codec<1>(), symmetric_metadata(level_schema()));
    runner.expose_component(t2, "q\"uote\\back", lua_tagged_codec<2>(), symmetric_metadata(level_schema()));
    runner.expose_component(t3, "\xC3\xA9", lua_tagged_codec<3>(), symmetric_metadata(level_schema()));
    runner.expose_component(t4, "_ok1", lua_tagged_codec<4>(), symmetric_metadata(level_schema()));

    const LuaManifestResult captured = runner.capability_manifest(world, behavior, 0);
    const LuaCapabilityManifest& manifest = require_manifest(captured);
    const std::vector<std::pair<std::string, std::string>> expected = {
        {"Light", R"(access.Light.office)"},
        {"_ok1", R"(access._ok1["1st"])"},
        {"end", R"(access["end"]["a-b"])"},
        {"q\"uote\\back", R"(access["q\"uote\\back"]["sp ace"])"},
        {"\xC3\xA9", R"(access["\195\169"]["\001\127"])"}
    };
    REQUIRE(manifest.capabilities().size() == expected.size());

    for (std::size_t index = 0; index < expected.size(); ++index) {
        const LuaManifestCapability& capability = manifest.capabilities()[index];
        const LuaManifestCapture::Entry& entry = manifest.capture().entries()[index];
        INFO(capability.accessExpression);
        REQUIRE(capability.scriptTypeName == expected[index].first);
        REQUIRE(capability.accessExpression == expected[index].second);
        REQUIRE(capability.accessExpression.size() <= LuaMaxAccessExpressionBytes);

        const std::string source =
            "local capability = " + capability.accessExpression + "\n"
            "assert(capability ~= nil)\n"
            "assert(capability.value.level == 1)\n"
            "capability.propose{name = 'p" + std::to_string(index) + "', value = {level = 2}}\n";
        const LuaExecutionResult result = runner.execute(world, behavior, 0, source);
        require_status(result, LuaExecutionStatus::Success);
        REQUIRE(result.createdIntents.size() == 1);
        REQUIRE(has_intent_on(world, entry.type, entry.slot, result.createdIntents.front()));
    }
}

TEST_CASE("L0.4 NUL component names keep their full byte identity and NUL binding names are rejected") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    const std::string nulName("a\0b", 3);
    world.add_component(lightType, "a", Light{1});
    world.add_component(lightType, nulName, Light{2});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(lightType, behavior, "a", ComponentAccessMode::ReadWrite);
    world.grant_component_access(lightType, behavior, nulName, ComponentAccessMode::ReadWrite);

    LuaBehaviorRunner runner;
    REQUIRE_THROWS_AS(
        runner.expose_component(lightType, std::string("Li\0ght", 6), lua_tagged_codec<0>(), symmetric_metadata(level_schema())),
        std::invalid_argument);
    runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema()));

    const LuaManifestResult captured = runner.capability_manifest(world, behavior, 0);
    const LuaCapabilityManifest& manifest = require_manifest(captured);
    REQUIRE(manifest.capabilities().size() == 2);
    REQUIRE(manifest.capabilities()[0].accessExpression == "access.Light.a");
    REQUIRE(manifest.capabilities()[1].componentName == nulName);
    REQUIRE(manifest.capabilities()[1].accessExpression == R"(access.Light["a\000b"])");

    const LuaExecutionResult result = runner.execute(world, behavior, 0,
        "local capability = " + manifest.capabilities()[1].accessExpression + "\n"
        "assert(capability.value.level == 2)\n"
        "capability.propose{name = 'nul', value = {level = 9}}\n");
    require_status(result, LuaExecutionStatus::Success);
    REQUIRE(result.createdIntents.size() == 1);
    const auto& slots = world.get_components(lightType, behavior);
    REQUIRE(has_intent_on(world, lightType.id, slots.at(nulName), result.createdIntents.front()));
    REQUIRE(!has_intent_on(world, lightType.id, slots.at("a"), result.createdIntents.front()));
}

TEST_CASE("L0.4 literal {} is an Object while a host empty Array round-trips") {
    World world;
    const auto samplesType = world.register_component<Samples>("example.Samples", 1, samples_codec());
    const auto settingsType = world.register_component<Settings>("example.Settings", 1, settings_codec());
    world.add_component(samplesType, "items", Samples{});
    world.add_component(settingsType, "prefs", Settings{});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(samplesType, behavior, "items", ComponentAccessMode::ReadWrite);
    world.grant_component_access(settingsType, behavior, "prefs", ComponentAccessMode::ReadWrite);

    std::size_t decodes = 0;
    LuaBehaviorRunner runner;
    runner.expose_component(samplesType, "Samples", lua_samples_codec(decodes), symmetric_metadata(samples_schema()));
    runner.expose_component(settingsType, "Settings", lua_settings_codec(),
        symmetric_metadata(Schema::object({{"on", Schema::boolean(), false}})));

    const LuaManifestResult captured = runner.capability_manifest(world, behavior, 0);
    const LuaCapabilityManifest& manifest = require_manifest(captured);
    const LuaManifestCapability& samples = manifest.capabilities().at(0);
    REQUIRE(samples.scriptTypeName == "Samples");
    REQUIRE(std::holds_alternative<LuaValue::Array>(samples.readValue->storage()));
    REQUIRE(samples.readValue->as_array().empty());

    const LuaExecutionResult roundTrip = runner.execute(world, behavior, 0, R"lua(
        assert(#access.Samples.items.value == 0)
        access.Samples.items.propose{name = "same", value = access.Samples.items.value}
    )lua");
    require_status(roundTrip, LuaExecutionStatus::Success);
    REQUIRE(decodes == 1);

    const LuaExecutionResult literal = runner.execute(world, behavior, 0,
        "access.Samples.items.propose{name = 'literal', value = {}}");
    require_status(literal, LuaExecutionStatus::InvalidProposal);
    REQUIRE(decodes == 1);

    const LuaExecutionResult object = runner.execute(world, behavior, 0,
        "access.Settings.prefs.propose{name = 'empty', value = {}}");
    require_status(object, LuaExecutionStatus::Success);
    REQUIRE(object.createdIntents.size() == 1);
}

TEST_CASE("L0.4 revoke, remove and mode change rebuild the manifest") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    world.add_component(lightType, "office", Light{3});
    const BehaviorId behavior = world.create_behavior();
    world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::ReadWrite);

    LuaBehaviorRunner runner;
    runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema()));

    const LuaManifestResult granted = runner.capability_manifest(world, behavior, 0);
    REQUIRE(require_manifest(granted).capabilities().size() == 1);
    REQUIRE(granted.manifest().capabilities().front().mode == ComponentAccessMode::ReadWrite);

    world.revoke_component_access(lightType, behavior, "office");
    const LuaManifestResult revoked = runner.capability_manifest(world, behavior, 0);
    REQUIRE(require_manifest(revoked).capabilities().empty());
    REQUIRE(revoked.manifest().capture().access_revision() != granted.manifest().capture().access_revision());
    REQUIRE(revoked.manifest().capture().access_revision() == world.behavior_access_revision(behavior));

    world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::Write);
    const LuaManifestResult writeOnly = runner.capability_manifest(world, behavior, 0);
    REQUIRE(require_manifest(writeOnly).capabilities().size() == 1);
    REQUIRE(writeOnly.manifest().capabilities().front().mode == ComponentAccessMode::Write);
    REQUIRE(!writeOnly.manifest().capabilities().front().readValue.has_value());

    world.remove_component(lightType, "office");
    const LuaManifestResult removed = runner.capability_manifest(world, behavior, 0);
    REQUIRE(require_manifest(removed).capabilities().empty());
}

TEST_CASE("L0.4 capability count at limit and limit+1") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    const BehaviorId behavior = world.create_behavior();
    for (std::size_t index = 0; index < LuaManifestMaxCapabilities; ++index) {
        world.add_component(lightType, component_name(index), Light{1});
        world.grant_component_access(lightType, behavior, component_name(index), ComponentAccessMode::Read);
    }

    LuaBehaviorRunner runner;
    runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema()));
    REQUIRE(require_manifest(runner.capability_manifest(world, behavior, 0)).capabilities().size()
        == LuaManifestMaxCapabilities);

    world.add_component(lightType, component_name(LuaManifestMaxCapabilities), Light{1});
    world.grant_component_access(lightType, behavior, component_name(LuaManifestMaxCapabilities), ComponentAccessMode::Read);
    require_manifest_error(runner.capability_manifest(world, behavior, 0), LuaManifestErrorCode::LimitExceeded);
}

TEST_CASE("L0.4 copied value nodes at limit and limit+1") {
    World world;
    const auto samplesType = world.register_component<Samples>("example.Samples", 1, samples_codec());
    const BehaviorId behavior = world.create_behavior();
    Samples full;
    full.values.assign(4095, 1);
    // 4 * (1 array + 4095 items) = 16,384 copied nodes.
    for (std::size_t index = 0; index < 4; ++index) {
        world.add_component(samplesType, component_name(index), full);
        world.grant_component_access(samplesType, behavior, component_name(index), ComponentAccessMode::Read);
    }

    std::size_t decodes = 0;
    LuaBehaviorRunner runner;
    runner.expose_component(samplesType, "Samples", lua_samples_codec(decodes), symmetric_metadata(samples_schema()));
    require_manifest(runner.capability_manifest(world, behavior, 0));

    world.add_component(samplesType, component_name(4), Samples{});
    world.grant_component_access(samplesType, behavior, component_name(4), ComponentAccessMode::Read);
    require_manifest_error(runner.capability_manifest(world, behavior, 0), LuaManifestErrorCode::LimitExceeded);
}

TEST_CASE("L0.4 manifest description bytes at limit and limit+1") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    const BehaviorId behavior = world.create_behavior();
    const std::size_t perCapability = LuaMaxDescriptionBytes;
    const std::size_t atLimit = LuaManifestMaxDescriptionBytes / perCapability;
    for (std::size_t index = 0; index < atLimit; ++index) {
        world.add_component(lightType, component_name(index), Light{1});
        world.grant_component_access(lightType, behavior, component_name(index), ComponentAccessMode::Read);
    }

    LuaBehaviorRunner runner;
    runner.expose_component(lightType, "Light", lua_tagged_codec<0>(),
        symmetric_metadata(level_schema(), std::string(perCapability, 'd')));
    require_manifest(runner.capability_manifest(world, behavior, 0));

    world.add_component(lightType, component_name(atLimit), Light{1});
    world.grant_component_access(lightType, behavior, component_name(atLimit), ComponentAccessMode::Read);
    require_manifest_error(runner.capability_manifest(world, behavior, 0), LuaManifestErrorCode::LimitExceeded);
}

TEST_CASE("L0.4 manifest logical bytes within and beyond 1 MiB") {
    World world;
    const auto textType = world.register_component<Text>("example.Text", 1, text_codec());
    const BehaviorId behavior = world.create_behavior();
    const Text large{std::string(LuaSchemaMaxStringBytes, 't')};
    const std::size_t within = LuaManifestMaxLogicalBytes / LuaSchemaMaxStringBytes - 1;
    for (std::size_t index = 0; index < within; ++index) {
        world.add_component(textType, component_name(index), large);
        world.grant_component_access(textType, behavior, component_name(index), ComponentAccessMode::Read);
    }

    LuaBehaviorRunner runner;
    runner.expose_component(textType, "Text", lua_text_codec(),
        symmetric_metadata(Schema::string(0, LuaSchemaMaxStringBytes)));
    require_manifest(runner.capability_manifest(world, behavior, 0));

    world.add_component(textType, component_name(within), large);
    world.grant_component_access(textType, behavior, component_name(within), ComponentAccessMode::Read);
    require_manifest_error(runner.capability_manifest(world, behavior, 0), LuaManifestErrorCode::LimitExceeded);
}

// An object of `rows` object fields with 256-byte names. Every row holds 255
// Boolean fields except the last, which holds `lastLeaves`.
Schema wide_schema(std::size_t rows, std::size_t lastLeaves) {
    const auto field_name = [](std::size_t index) {
        std::string digits = std::to_string(index);
        return std::string(LuaMaxNameBytes - 4, 'f') + std::string(4 - digits.size(), '0') + digits;
    };
    std::vector<LuaSchemaField> fields;
    for (std::size_t row = 0; row < rows; ++row) {
        std::vector<LuaSchemaField> leaves;
        for (std::size_t index = 0; index < (row + 1 == rows ? lastLeaves : 255); ++index)
            leaves.push_back({field_name(index), Schema::boolean()});
        fields.push_back({field_name(row), Schema::object(leaves)});
    }
    return Schema::object(fields);
}

TEST_CASE("L0.4 schema-owned bytes count toward the 1 MiB logical limit") {
    // Contract accounting: 8 per numeric scalar, ID or enum, 1 per boolean
    // or presence flag, string byte length, 8 per collection element or
    // record field.
    // Fixed manifest fields = 930:
    //   5 fields * 8 + "liquid.lua.authoring/1" (22) + now_ms (8)
    //   + limits (12 fields * 8 + 11 * 8 + 1 = 185)
    //   + notes (7 * 8 + 619 bytes = 675)
    // Write-only capability fixed = 8 element + 8 fields * 8 + mode 8
    //   + 3 presence flags = 83; "Light" 5 + "office" 6
    //   + "access.Light.office" 19 = 30.
    // Schema: kind tag 8 + description; each object field 8 + name 256
    //   + required flag 1 + child; a Boolean leaf is 8.
    //   leaf field = 273; full row field = 8 + 256 + 1 + 8 + 255 * 273 = 69,888
    //   last row field (252 leaves) = 273 + 252 * 273 = 69,069
    //   root = 8 + 14 * 69,888 + 69,069 = 1,047,509
    // 930 + 83 + 30 + 1,047,509 = 1,048,552, so a 24-byte description
    // totals exactly 1,048,576 bytes and 25 bytes is one byte over.
    const Schema large = wide_schema(15, 252);

    const auto capture = [&](std::size_t descriptionBytes) {
        World world;
        const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
        world.add_component(lightType, "office", Light{1});
        const BehaviorId behavior = world.create_behavior();
        world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::Write);

        LuaBehaviorRunner runner;
        runner.expose_component(lightType, "Light", lua_tagged_codec<0>(),
            symmetric_metadata(large, std::string(descriptionBytes, 'd')));
        return runner.capability_manifest(world, behavior, 0);
    };

    const LuaManifestResult atLimit = capture(24);
    const LuaCapabilityManifest& manifest = require_manifest(atLimit);
    REQUIRE(manifest.capabilities().size() == 1);
    REQUIRE(!manifest.capabilities().front().readSchema.has_value());
    std::size_t noteBytes = 0;
    for (const std::string& note : manifest.notes())
        noteBytes += 8 + note.size();
    REQUIRE(noteBytes == 675);
    require_manifest_error(capture(25), LuaManifestErrorCode::LimitExceeded);
}

TEST_CASE("L0.4 numeric read values count toward the 1 MiB logical limit") {
    // A write-only Light with a 14-row wide schema (8 + 14 * 69,888 =
    // 978,440 bytes) plus two readable Samples of 4,096 integers. Each
    // integer element costs 8 (element) + 8 (scalar), so each full value is
    // 65,536 bytes and the manifest totals 1,110,913 bytes; with empty
    // arrays it totals 979,841 bytes.
    const auto capture = [](std::size_t count) {
        World world;
        const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
        const auto samplesType = world.register_component<Samples>("example.Samples", 1, samples_codec());
        const BehaviorId behavior = world.create_behavior();
        world.add_component(lightType, "office", Light{1});
        world.grant_component_access(lightType, behavior, "office", ComponentAccessMode::Write);
        Samples samples;
        samples.values.assign(count, 7);
        for (std::size_t index = 0; index < 2; ++index) {
            world.add_component(samplesType, component_name(index), samples);
            world.grant_component_access(samplesType, behavior, component_name(index), ComponentAccessMode::Read);
        }

        std::size_t decodes = 0;
        LuaBehaviorRunner runner;
        runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(wide_schema(14, 255)));
        runner.expose_component(samplesType, "Samples", lua_samples_codec(decodes), symmetric_metadata(samples_schema()));
        return runner.capability_manifest(world, behavior, 0);
    };

    REQUIRE(require_manifest(capture(0)).capabilities().size() == 3);
    require_manifest_error(capture(4096), LuaManifestErrorCode::LimitExceeded);
}

TEST_CASE("L0.4 component name bytes and expression bytes at the L0 ceiling") {
    World world;
    const auto lightType = world.register_component<Light>("example.Light", 1, tagged_codec<0>());
    const auto controlType = world.register_component<Tagged<1>>("example.Control", 1, tagged_codec<1>());
    const BehaviorId behavior = world.create_behavior();
    const std::string longName(LuaMaxNameBytes, 'n');
    const std::string controlName(LuaMaxNameBytes, '\x01');
    world.add_component(lightType, longName, Light{1});
    world.grant_component_access(lightType, behavior, longName, ComponentAccessMode::ReadWrite);
    world.add_component(controlType, controlName, Tagged<1>{1});
    world.grant_component_access(controlType, behavior, controlName, ComponentAccessMode::ReadWrite);

    LuaBehaviorRunner runner;
    runner.expose_component(lightType, "Light", lua_tagged_codec<0>(), symmetric_metadata(level_schema()));
    runner.expose_component(controlType, controlName, lua_tagged_codec<1>(), symmetric_metadata(level_schema()));

    const LuaManifestResult captured = runner.capability_manifest(world, behavior, 0);
    const LuaCapabilityManifest& manifest = require_manifest(captured);
    REQUIRE(manifest.capabilities().size() == 2);
    // Worst case for 256-byte names: every byte needs a three-digit escape.
    const LuaManifestCapability& control = manifest.capabilities()[0];
    REQUIRE(control.scriptTypeName == controlName);
    REQUIRE(control.accessExpression.size() == 6 + 2 * (4 + 4 * LuaMaxNameBytes));
    REQUIRE(control.accessExpression.size() <= LuaMaxAccessExpressionBytes);
    REQUIRE(manifest.capabilities()[1].accessExpression == "access.Light." + longName);

    const LuaExecutionResult result = runner.execute(world, behavior, 0,
        "local capability = " + control.accessExpression + "\n"
        "capability.propose{name = 'long', value = {level = 2}}\n");
    require_status(result, LuaExecutionStatus::Success);
    REQUIRE(has_intent_on(world, controlType.id, manifest.capture().entries()[0].slot, result.createdIntents.front()));

    const std::string tooLong(LuaMaxNameBytes + 1, 'n');
    world.add_component(lightType, tooLong, Light{1});
    world.grant_component_access(lightType, behavior, tooLong, ComponentAccessMode::Read);
    require_manifest_error(runner.capability_manifest(world, behavior, 0), LuaManifestErrorCode::LimitExceeded);
}
