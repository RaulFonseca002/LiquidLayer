#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
class BulkProbeA {
public:
    void begin();
    void update() { _ticks++; }
    uint32_t footprintKB() const { return (uint32_t)sizeof(*this) / 1024; }
private:
    struct Slot { uint16_t len; int8_t rssi, noise; uint8_t ch, nAnt; uint32_t seq; int8_t iq[1024]; };
    Slot        _ring[16];
    float       _hist[3][256];
    double      _var[128][3];
    float       _prev[128];
    char        _logQ[24][96];
    uint8_t     _txBuf[1044];
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    volatile uint32_t _ticks = 0;
};
