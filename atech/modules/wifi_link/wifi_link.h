// wifi_link.h — WiFi station + NVS credentials + serial actions + 1 Hz alive beacon.
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <esp_wifi.h>

class WifiLink {
public:
    enum class State : uint8_t { Unconfigured, Idle, Connecting, Connected, Lost };
    static constexpr uint32_t LATE_START_MS = 1500;
    static constexpr uint32_t BEACON_MAGIC  = 0xA11E0001u;

    explicit WifiLink(const char* instanceName);
    void begin();
    void update();

    State       state() const { return _state; }
    const char* stateName() const;
    bool        isConnected() const { return _state == State::Connected; }
    int         rssi() const { return _rssi; }
    const char* ipString() const { return _ip; }
    const char* sinkIp() const { return _sinkIp; }
    uint16_t    sinkPort() const { return _sinkPort; }
    uint32_t    beaconsSent() const { return _beacons; }
    bool        nextEvent(char* out, size_t cap);

    void setWifi(const char* ssid, const char* pass);
    void setSink(const char* ip, uint16_t port);
    void scanNetworks();
    void logEvent(const char* msg);

private:
    void loadConfig();
    void startWifi();
    void sendBeacon(uint32_t nowMs);
    static void onActionStatic(const char* a, const char* v, void* ctx);
    void onAction(const char* a, const char* v);

    char        _name[24];
    State       _state = State::Unconfigured;
    uint32_t    _bootMs = 0, _connectStartMs = 0, _lastBeaconMs = 0, _lastEvMs = 0;
    int         _rssi = 0;
    char        _ip[16] = "0.0.0.0";
    uint32_t    _beacons = 0, _seq = 0;
    uint8_t     _evIdx = 0;

    Preferences _prefs;
    char        _ssid[33] = {0};
    char        _pass[65] = {0};
    char        _sinkIp[16] = "192.168.1.100";
    uint16_t    _sinkPort = 5005;
    IPAddress   _sinkAddr;
    bool        _sinkValid = false;
    WiFiUDP     _udp;
    bool        _udpUp = false;

    static constexpr uint8_t LOGQ = 16;
    static constexpr size_t  LOGQ_LEN = 96;
    char        _logQ[LOGQ][LOGQ_LEN];
    uint8_t     _logHead = 0, _logCount = 0;
};
