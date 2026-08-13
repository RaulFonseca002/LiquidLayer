#include "FuzzSupport.hpp"

#include "liquid/events/ValueCodec.hpp"

#include <stdexcept>

int liquid_fuzz_one_input(std::span<const std::uint8_t> input) {
    try {
        const liquid::Value decoded = liquid::decode_value(input);
        const auto canonical = liquid::encode_value(decoded);
        const liquid::Value roundTrip = liquid::decode_value(canonical);
        if (roundTrip != decoded)
            std::terminate();
    } catch (const std::invalid_argument&) {
    }
    return 0;
}
