// SPI.h — host stand-in for the Arduino SPI bus object (no-op recorder).
#pragma once
#include <Arduino.h>
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3
class SPISettings {
public:
    SPISettings(uint32_t = 0, uint8_t = 0, uint8_t = 0) {}
};
class SPIClass {
public:
    void begin(int8_t = -1, int8_t = -1, int8_t = -1, int8_t = -1) { sim::trace("spi   ", "SPI.begin()"); }
    void end() {}
    void beginTransaction(SPISettings) {}
    void endTransaction() {}
    uint8_t transfer(uint8_t) { return 0; }
    uint16_t transfer16(uint16_t) { return 0; }
    void transfer(void*, size_t) {}
    void setFrequency(uint32_t) {}
};
inline SPIClass SPI;
