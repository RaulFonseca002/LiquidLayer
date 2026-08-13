#include "liquid/Value.hpp"

#include <cmath>
#include <stdexcept>

namespace liquid {
namespace {

bool valid_utf8(const std::string& text) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    std::size_t position = 0;

    while (position < text.size()) {
        unsigned char first = bytes[position];

        if (first <= 0x7fU) {
            ++position;
            continue;
        }

        std::size_t continuationCount = 0;
        std::uint32_t codePoint = 0;

        if (first >= 0xc2U && first <= 0xdfU) {
            continuationCount = 1;
            codePoint = first & 0x1fU;
        } else if (first >= 0xe0U && first <= 0xefU) {
            continuationCount = 2;
            codePoint = first & 0x0fU;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            continuationCount = 3;
            codePoint = first & 0x07U;
        } else {
            return false;
        }

        if (position + continuationCount >= text.size())
            return false;

        for (std::size_t offset = 1; offset <= continuationCount; ++offset) {
            unsigned char continuation = bytes[position + offset];

            if ((continuation & 0xc0U) != 0x80U)
                return false;

            codePoint = (codePoint << 6U) | (continuation & 0x3fU);
        }

        if ((continuationCount == 2 && codePoint < 0x800U) ||
            (continuationCount == 3 && codePoint < 0x10000U) ||
            codePoint > 0x10ffffU ||
            (codePoint >= 0xd800U && codePoint <= 0xdfffU)) {
            return false;
        }

        position += continuationCount + 1;
    }

    return true;
}

void validate_value(const Value& value, std::size_t depth, std::size_t& nodes) {
    if (depth > ValueLimits::maxDepth)
        throw std::invalid_argument("value nesting limit exceeded");

    if (++nodes > ValueLimits::maxTotalNodes)
        throw std::invalid_argument("value node limit exceeded");

    switch (value.kind()) {
    case Value::Kind::Null:
    case Value::Kind::Boolean:
    case Value::Kind::SignedInteger:
    case Value::Kind::UnsignedInteger:
        return;
    case Value::Kind::Double:
        if (!std::isfinite(value.as_double()))
            throw std::invalid_argument("value double must be finite");
        return;
    case Value::Kind::String: {
        const std::string& text = value.as_string();

        if (text.size() > ValueLimits::maxStringBytes)
            throw std::invalid_argument("value string limit exceeded");

        if (!valid_utf8(text))
            throw std::invalid_argument("value string is not valid UTF-8");

        return;
    }
    case Value::Kind::Bytes:
        if (value.as_bytes().size() > ValueLimits::maxByteStringBytes)
            throw std::invalid_argument("value byte-string limit exceeded");
        return;
    case Value::Kind::Array: {
        const Value::Array& values = value.as_array();

        if (values.size() > ValueLimits::maxArrayItems)
            throw std::invalid_argument("value array limit exceeded");

        for (const Value& child : values)
            validate_value(child, depth + 1, nodes);

        return;
    }
    case Value::Kind::Object: {
        const Value::Object& values = value.as_object();

        if (values.size() > ValueLimits::maxObjectItems)
            throw std::invalid_argument("value object limit exceeded");

        for (const auto& [key, child] : values) {
            if (key.size() > ValueLimits::maxStringBytes || !valid_utf8(key))
                throw std::invalid_argument("value object key is invalid");

            validate_value(child, depth + 1, nodes);
        }
    }
    }
}

}

Value::Value(bool value)
    : storedValue(value) {
}

Value::Value(std::int64_t value)
    : storedValue(value) {
}

Value::Value(std::uint64_t value)
    : storedValue(value) {
}

Value::Value(double value)
    : storedValue(value == 0.0 ? 0.0 : value) {
    validate();
}

Value::Value(std::string value)
    : storedValue(std::move(value)) {
    validate();
}

Value::Value(const char* value)
    : Value(std::string(value ? value : "")) {
}

Value::Value(Bytes value)
    : storedValue(std::move(value)) {
    validate();
}

Value::Value(Array value)
    : storedValue(std::move(value)) {
    validate();
}

Value::Value(Object value)
    : storedValue(std::move(value)) {
    validate();
}

Value::Value(Array value, Unchecked)
    : storedValue(std::move(value)) {
}

Value::Value(Object value, Unchecked)
    : storedValue(std::move(value)) {
}

Value::Kind Value::kind() const {
    return static_cast<Kind>(storedValue.index());
}

bool Value::is_null() const {
    return std::holds_alternative<Null>(storedValue);
}

bool Value::as_boolean() const {
    return std::get<bool>(storedValue);
}

std::int64_t Value::as_signed_integer() const {
    return std::get<std::int64_t>(storedValue);
}

std::uint64_t Value::as_unsigned_integer() const {
    return std::get<std::uint64_t>(storedValue);
}

double Value::as_double() const {
    return std::get<double>(storedValue);
}

const std::string& Value::as_string() const {
    return std::get<std::string>(storedValue);
}

const Value::Bytes& Value::as_bytes() const {
    return std::get<Bytes>(storedValue);
}

const Value::Array& Value::as_array() const {
    return std::get<Array>(storedValue);
}

const Value::Object& Value::as_object() const {
    return std::get<Object>(storedValue);
}

void Value::validate() const {
    std::size_t nodes = 0;
    validate_value(*this, 0, nodes);
}

}
