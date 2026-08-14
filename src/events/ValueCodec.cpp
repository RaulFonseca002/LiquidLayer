#include "liquid/events/ValueCodec.hpp"

#include <bit>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace liquid {
namespace {

enum class ValueTag : std::uint8_t {
    Null = 0,
    False = 1,
    True = 2,
    SignedInteger = 3,
    UnsignedInteger = 4,
    Double = 5,
    String = 6,
    Bytes = 7,
    Array = 8,
    Object = 9
};

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        output.push_back(static_cast<std::uint8_t>(value >> shift));
}

void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8)
        output.push_back(static_cast<std::uint8_t>(value >> shift));
}

std::uint32_t checked_size(std::size_t size) {
    if (size > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("value is too large to encode");

    return static_cast<std::uint32_t>(size);
}

void encode(const Value& value, std::vector<std::uint8_t>& output) {
    switch (value.kind()) {
    case Value::Kind::Null:
        output.push_back(static_cast<std::uint8_t>(ValueTag::Null));
        return;
    case Value::Kind::Boolean:
        output.push_back(static_cast<std::uint8_t>(
            value.as_boolean() ? ValueTag::True : ValueTag::False));
        return;
    case Value::Kind::SignedInteger:
        output.push_back(static_cast<std::uint8_t>(ValueTag::SignedInteger));
        append_u64(output, std::bit_cast<std::uint64_t>(value.as_signed_integer()));
        return;
    case Value::Kind::UnsignedInteger:
        output.push_back(static_cast<std::uint8_t>(ValueTag::UnsignedInteger));
        append_u64(output, value.as_unsigned_integer());
        return;
    case Value::Kind::Double:
        output.push_back(static_cast<std::uint8_t>(ValueTag::Double));
        append_u64(output, std::bit_cast<std::uint64_t>(value.as_double()));
        return;
    case Value::Kind::String: {
        output.push_back(static_cast<std::uint8_t>(ValueTag::String));
        const std::string& text = value.as_string();
        append_u32(output, checked_size(text.size()));
        output.insert(output.end(), text.begin(), text.end());
        return;
    }
    case Value::Kind::Bytes: {
        output.push_back(static_cast<std::uint8_t>(ValueTag::Bytes));
        const Value::Bytes& bytes = value.as_bytes();
        append_u32(output, checked_size(bytes.size()));
        output.insert(output.end(), bytes.begin(), bytes.end());
        return;
    }
    case Value::Kind::Array: {
        output.push_back(static_cast<std::uint8_t>(ValueTag::Array));
        const Value::Array& array = value.as_array();
        append_u32(output, checked_size(array.size()));

        for (const Value& child : array)
            encode(child, output);

        return;
    }
    case Value::Kind::Object: {
        output.push_back(static_cast<std::uint8_t>(ValueTag::Object));
        const Value::Object& object = value.as_object();
        append_u32(output, checked_size(object.size()));

        for (const auto& [key, child] : object) {
            append_u32(output, checked_size(key.size()));
            output.insert(output.end(), key.begin(), key.end());
            encode(child, output);
        }
    }
    }
}

class Decoder {
private:
    std::span<const std::uint8_t> input;
    std::size_t position = 0;
    std::size_t nodes = 0;

    void require(std::size_t count) const {
        if (count > input.size() - position)
            throw std::invalid_argument("truncated encoded value");
    }

    std::uint8_t read_u8() {
        require(1);
        return input[position++];
    }

    std::uint32_t read_u32() {
        require(4);
        std::uint32_t value = 0;

        for (unsigned shift = 0; shift < 32; shift += 8)
            value |= static_cast<std::uint32_t>(input[position++]) << shift;

        return value;
    }

    std::uint64_t read_u64() {
        require(8);
        std::uint64_t value = 0;

        for (unsigned shift = 0; shift < 64; shift += 8)
            value |= static_cast<std::uint64_t>(input[position++]) << shift;

        return value;
    }

    std::string read_string(std::size_t limit) {
        const std::size_t size = read_u32();

        if (size > limit)
            throw std::invalid_argument("encoded value string limit exceeded");

        require(size);
        const auto* begin = reinterpret_cast<const char*>(input.data() + position);
        std::string result(begin, size);
        position += size;
        return result;
    }

public:
    explicit Decoder(std::span<const std::uint8_t> encoded)
        : input(encoded) {
    }

    Value decode(std::size_t depth = 0) {
        if (depth > ValueLimits::maxDepth)
            throw std::invalid_argument("encoded value nesting limit exceeded");

        if (++nodes > ValueLimits::maxTotalNodes)
            throw std::invalid_argument("encoded value node limit exceeded");

        switch (static_cast<ValueTag>(read_u8())) {
        case ValueTag::Null:
            return Value();
        case ValueTag::False:
            return Value(false);
        case ValueTag::True:
            return Value(true);
        case ValueTag::SignedInteger:
            return Value(std::bit_cast<std::int64_t>(read_u64()));
        case ValueTag::UnsignedInteger:
            return Value(read_u64());
        case ValueTag::Double:
            return Value(std::bit_cast<double>(read_u64()));
        case ValueTag::String:
            return Value(read_string(ValueLimits::maxStringBytes));
        case ValueTag::Bytes: {
            const std::size_t size = read_u32();

            if (size > ValueLimits::maxByteStringBytes)
                throw std::invalid_argument("encoded byte-string limit exceeded");

            require(size);
            Value::Bytes bytes(input.begin() + static_cast<std::ptrdiff_t>(position),
                               input.begin() + static_cast<std::ptrdiff_t>(position + size));
            position += size;
            return Value(std::move(bytes));
        }
        case ValueTag::Array: {
            const std::size_t count = read_u32();

            if (count > ValueLimits::maxArrayItems)
                throw std::invalid_argument("encoded array limit exceeded");

            Value::Array array;
            array.reserve(count);

            for (std::size_t index = 0; index < count; ++index)
                array.push_back(decode(depth + 1));

            return Value(std::move(array), Value::Unchecked{});
        }
        case ValueTag::Object: {
            const std::size_t count = read_u32();

            if (count > ValueLimits::maxObjectItems)
                throw std::invalid_argument("encoded object limit exceeded");

            Value::Object object;

            for (std::size_t index = 0; index < count; ++index) {
                std::string key = read_string(ValueLimits::maxStringBytes);
                const auto [iterator, inserted] =
                    object.emplace(std::move(key), decode(depth + 1));
                static_cast<void>(iterator);

                if (!inserted)
                    throw std::invalid_argument("encoded object has duplicate key");
            }

            return Value(std::move(object), Value::Unchecked{});
        }
        }

        throw std::invalid_argument("encoded value has unknown type tag");
    }

    bool finished() const {
        return position == input.size();
    }
};

}

std::vector<std::uint8_t> encode_value(const Value& value) {
    value.validate();
    std::vector<std::uint8_t> output;
    encode(value, output);
    return output;
}

Value decode_value(std::span<const std::uint8_t> encoded) {
    Decoder decoder(encoded);
    Value value = decoder.decode();

    if (!decoder.finished())
        throw std::invalid_argument("encoded value has trailing bytes");

    value.validate();
    return value;
}

}
