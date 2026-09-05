// Wire.h — host stand-in for the Arduino I2C bus object. Records traffic;
// reads return zeros. Real sensor drivers are mocked at module level anyway.
#pragma once
#include <Arduino.h>
#include <vector>

class TwoWire {
public:
    bool begin(int sda = -1, int scl = -1, uint32_t freq = 0) { sim::trace("i2c   ", "Wire.begin(sda=" + std::to_string(sda) + ", scl=" + std::to_string(scl) + ")"); return true; }
    void end() {}
    void setClock(uint32_t hz) { sim::trace("i2c   ", "Wire.setClock(" + std::to_string(hz) + ")"); }
    void setTimeOut(uint16_t) {}
    void beginTransmission(uint8_t addr) { _addr = addr; _tx.clear(); }
    size_t write(uint8_t b) { _tx.push_back(b); return 1; }
    size_t write(const uint8_t* d, size_t n) { _tx.insert(_tx.end(), d, d + n); return n; }
    uint8_t endTransmission(bool = true) { sim::trace("i2c   ", "write addr=0x" + hex(_addr) + " " + std::to_string(_tx.size()) + " bytes"); return 0; }
    uint8_t requestFrom(uint8_t addr, uint8_t n, bool = true) { _rx = n; sim::trace("i2c   ", "read  addr=0x" + hex(addr) + " " + std::to_string(n) + " bytes -> zeros"); return n; }
    int available() { return _rx; }
    int read() { if (_rx > 0) { --_rx; return 0; } return -1; }
    int peek() { return _rx > 0 ? 0 : -1; }
    void flush() {}
private:
    static std::string hex(uint8_t v) { char b[8]; std::snprintf(b, sizeof b, "%02X", v); return b; }
    uint8_t _addr = 0;
    int _rx = 0;
    std::vector<uint8_t> _tx;
};
inline TwoWire Wire;
inline TwoWire Wire1;
