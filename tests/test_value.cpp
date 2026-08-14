#include "liquid/Value.hpp"
#include "liquid/events/ValueCodec.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

using namespace liquid;

namespace {

template <typename Function>
void expect_invalid(Function function) {
    bool thrown = false;

    try {
        function();
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    REQUIRE(thrown);
}

}

TEST_CASE("bounded Value semantics") {
    using liquid::Value;
    using liquid::ValueLimits;

    Value::Object firstObject;
    firstObject.emplace("zeta", Value(std::int64_t{2}));
    firstObject.emplace("alpha", Value("ready"));

    Value::Object secondObject;
    secondObject.emplace("alpha", Value("ready"));
    secondObject.emplace("zeta", Value(std::int64_t{2}));

    Value first(std::move(firstObject));
    Value second(std::move(secondObject));
    REQUIRE(first == second);

    std::string source = "before";
    Value::Array sourceArray{Value(source)};
    Value snapshot(sourceArray);
    source = "after";
    sourceArray.front() = Value("changed");
    REQUIRE(snapshot.as_array().front().as_string() == "before");

    Value bytes(Value::Bytes{0, 1, 127, 255});
    REQUIRE(bytes.as_bytes().size() == 4);
    REQUIRE(bytes.as_bytes().back() == 255);

    REQUIRE(Value().is_null());
    REQUIRE(Value(true).as_boolean());
    REQUIRE(Value(std::int64_t{-4}).as_signed_integer() == -4);
    REQUIRE(Value(std::uint64_t{9}).as_unsigned_integer() == 9);
    REQUIRE(Value(1.5).as_double() == 1.5);
    const Value negativeZero{-0.0};
    const Value positiveZero{0.0};
    REQUIRE(!std::signbit(negativeZero.as_double()));
    REQUIRE(liquid::encode_value(negativeZero) ==
            liquid::encode_value(positiveZero));

    expect_invalid([] { Value value(std::numeric_limits<double>::infinity()); });
    expect_invalid([] { Value value(std::numeric_limits<double>::quiet_NaN()); });
    expect_invalid([] { Value value(std::string("\xC3\x28", 2)); });
    expect_invalid([] {
        Value value(std::string(ValueLimits::maxStringBytes + 1, 'x'));
    });
    expect_invalid([] {
        Value::Array nested;
        Value current;

        for (std::size_t depth = 0; depth <= ValueLimits::maxDepth; ++depth) {
            nested.clear();
            nested.push_back(std::move(current));
            current = Value(std::move(nested), Value::Unchecked{});
        }

        current.validate();
    });

}
