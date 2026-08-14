#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace liquid {

struct ValueLimits {
    static constexpr std::size_t maxDepth = 32;
    static constexpr std::size_t maxStringBytes = 1024 * 1024;
    static constexpr std::size_t maxByteStringBytes = 1024 * 1024;
    static constexpr std::size_t maxArrayItems = 4096;
    static constexpr std::size_t maxObjectItems = 4096;
    static constexpr std::size_t maxTotalNodes = 16384;
};

class Value {
public:
    using Null = std::monostate;
    using Bytes = std::vector<std::uint8_t>;
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    struct Unchecked {};

    enum class Kind {
        Null,
        Boolean,
        SignedInteger,
        UnsignedInteger,
        Double,
        String,
        Bytes,
        Array,
        Object
    };

private:
    using Storage = std::variant<
        Null,
        bool,
        std::int64_t,
        std::uint64_t,
        double,
        std::string,
        Bytes,
        Array,
        Object
    >;

    Storage storedValue;

public:
    Value() = default;
    explicit Value(bool value);
    explicit Value(std::int64_t value);
    explicit Value(std::uint64_t value);
    explicit Value(double value);
    explicit Value(std::string value);
    explicit Value(const char* value);
    explicit Value(Bytes value);
    explicit Value(Array value);
    explicit Value(Object value);

    Value(Array value, Unchecked);
    Value(Object value, Unchecked);

    Kind kind() const;
    bool is_null() const;
    bool as_boolean() const;
    std::int64_t as_signed_integer() const;
    std::uint64_t as_unsigned_integer() const;
    double as_double() const;
    const std::string& as_string() const;
    const Bytes& as_bytes() const;
    const Array& as_array() const;
    const Object& as_object() const;

    void validate() const;

    bool operator==(const Value& other) const = default;
};

}
