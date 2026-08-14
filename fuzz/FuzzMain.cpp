#include "FuzzSupport.hpp"

#include <array>

int main(int argc, char** argv) {
    if (argc == 1) {
        static constexpr std::array<std::uint8_t, 1> seed{0};
        return liquid_fuzz_one_input(seed);
    }

    for (int index = 1; index < argc; ++index) {
        const auto bytes = liquid_read_fuzz_seed(argv[index]);
        liquid_fuzz_one_input(bytes);
    }
    return 0;
}
