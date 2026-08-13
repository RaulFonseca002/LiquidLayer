#pragma once

#include "liquid/events/FileEventStore.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

inline liquid::EventStoreMetadata liquid_fuzz_metadata() {
    liquid::EventStoreMetadata metadata;
    metadata.session = liquid::SessionId{0x1122334455667788ULL};
    metadata.engineVersion = "0.1.0-fuzz";
    return metadata;
}

class LiquidFuzzFile {
    std::filesystem::path filePath;

public:
    explicit LiquidFuzzFile(const char* name) {
        static const std::string nonce = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        filePath = std::filesystem::temp_directory_path() /
            (std::string("liquid-") + name + "-" + nonce + ".bin");
        cleanup();
    }

    ~LiquidFuzzFile() {
        cleanup();
    }

    const std::filesystem::path& path() const {
        return filePath;
    }

    void write(std::span<const std::uint8_t> bytes, bool append = false) const {
        std::ofstream output(
            filePath,
            std::ios::binary | (append ? std::ios::app : std::ios::trunc));
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    void cleanup() const {
        std::error_code ignored;
        std::filesystem::remove(filePath, ignored);
        std::filesystem::remove(filePath.string() + ".lock", ignored);
        std::filesystem::remove(filePath.string() + ".replacement", ignored);
    }
};
