// link_probe.h — links the WiFi/lwIP/ping stacks into the build and does
// nothing with them at runtime (no radio start, no sockets opened).
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <ping/ping_sock.h>
class LinkProbe {
public:
    void begin();
    void update() {}
    uint32_t sketchKB() const { return ESP.getSketchSize() / 1024; }
    int mode() const { return _mode; }
private:
    int _mode = -1;
};
