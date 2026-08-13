#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <vector>

int liquid_fuzz_one_input(std::span<const std::uint8_t> input);

#ifdef LIQUID_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    return liquid_fuzz_one_input(std::span<const std::uint8_t>(data, size));
}
#endif

inline std::vector<std::uint8_t> liquid_read_fuzz_seed(const char* path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input), {});
}
