#include "FileFuzzSupport.hpp"
#include "FuzzSupport.hpp"

#include <stdexcept>

int liquid_fuzz_one_input(std::span<const std::uint8_t> input) {
    if (input.size() > liquid::EventLimits::maxBatchBytes * 2)
        return 0;

    LiquidFuzzFile file("batch-fuzz");
    try {
        {
            liquid::FileEventStore header(file.path(), liquid_fuzz_metadata());
        }
        file.write(input, true);
        liquid::FileEventStore parsed(file.path(), liquid_fuzz_metadata());
        static_cast<void>(parsed.read_all());
    } catch (const liquid::EventStoreError&) {
    } catch (const std::invalid_argument&) {
    }
    return 0;
}
