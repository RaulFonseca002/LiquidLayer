#include "FileFuzzSupport.hpp"
#include "FuzzSupport.hpp"

#include <stdexcept>
#include <utility>

int liquid_fuzz_one_input(std::span<const std::uint8_t> input) {
    if (input.size() > liquid::EventLimits::maxFileBytes)
        return 0;

    LiquidFuzzFile file("store-fuzz");
    file.write(input);
    try {
        liquid::FileEventStoreOptions options;
        options.writableRecovery = true;
        liquid::FileEventStore parsed(
            file.path(), liquid_fuzz_metadata(), std::move(options));
        static_cast<void>(parsed.read_all());
    } catch (const liquid::EventStoreError&) {
    } catch (const std::invalid_argument&) {
    }
    return 0;
}
