#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
class BulkProbeB {
public:
    void begin();
    void update() { _ticks++; }
    uint32_t footprintKB() const { return (uint32_t)sizeof(*this) / 1024; }
private:
    Preferences _prefs;
    WiFiUDP     _udp;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    volatile uint32_t _ticks = 0;
};
