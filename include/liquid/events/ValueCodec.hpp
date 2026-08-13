#pragma once

#include "liquid/Value.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace liquid {

std::vector<std::uint8_t> encode_value(const Value& value);
Value decode_value(std::span<const std::uint8_t> encoded);

}
