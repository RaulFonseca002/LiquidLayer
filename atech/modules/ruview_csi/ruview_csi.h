/**
 * @file ruview_csi.h
 * @brief RuView-compatible WiFi CSI sensing node as an Atech module.
 *
 * Milestone A scope: WiFi station + CSI capture on management frames, frame
 * statistics, ADR-018 UDP streaming to a RuView sink, a light on-device
 * activity/presence estimate, NVS-backed credentials and serial actions.
 * The Tier-2 vitals DSP (breathing / heart rate) arrives in Milestone B.
 *
 * Threading: the CSI callback runs on the WiFi task (core 0). It only stamps
 * statistics and copies the frame into a lock-free ring. Everything else
 * (UDP send, activity estimate, serial) happens in update() from loop().
 * Never touch SPI displays from the callback.
 *
 * Ported from github.com/ruvnet/RuView firmware/esp32-csi-node (MIT):
 *   csi_collector.c (CSI config, 50 Hz early gate, ADR-018 serialization).
 */
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include "ruview_wire.h"
#include "ruview_edge.h"

class RuViewCsi {
public:
    enum class State : uint8_t { Unconfigured, Connecting, Streaming, Lost };

    static constexpr uint8_t  RING_SLOTS        = 16;
    static constexpr uint32_t MIN_PROCESS_US    = 20 * 1000;  // 50 Hz early gate (RuView CSI_MIN_PROCESS_INTERVAL_US)
    static constexpr uint32_t PUMP_INTERVAL_MS  = 50;         // ICMP ping to the gateway -> 20 CSI frames/s
    static constexpr uint32_t PROBE_INTERVAL_MS = 100;        // probe-request injection fallback (10 Hz)
    static constexpr float    LOW_RATE_HZ       = 12.0f;      // below this, enable probe injection
    static constexpr uint8_t  LOGQ              = 24;         // queued log lines (drained one per event tick)
    static constexpr size_t   LOGQ_LEN          = 96;

    explicit RuViewCsi(const char* instanceName);

    void begin();
    void update();

    // ---- status ----
    State       state() const { return _state; }
    const char* stateName() const;
    bool        isConnected() const { return _state == State::Streaming; }
    float       frameRateHz() const { return _rateHz; }
    int         rssi() const { return _lastRssi; }
    float       activity() const { return _edge.motionEnergy(); }
    bool        presence() const { return _edge.presence(); }
    bool        isCalibrating() const { return _edge.calibrating(); }
    uint32_t    framesTotal() const { return _framesTotal; }
    float       heartRate() const { return _edge.heartRateBpm(); }
    float       breathingRate() const { return _edge.breathingBpm(); }
    bool        fall() const { return _edge.fall(); }
    bool        vitalsCalibrating() const { return _edge.calibrating(); }
    bool        consumeBeat() { return _edge.consumeBeat(); }
    uint32_t    packetsSent() const { return _packetsSent; }
    uint32_t    droppedFrames() const { return _dropped; }
    const char* sinkIp() const { return _sinkIp; }
    uint8_t     nodeId() const { return _nodeId; }
    bool        tick1Hz();  // true once per second
    // Paced event emitter: fills `out` with ONE JSON event line at most every
    // 200 ms, cycling frame_rate, rssi, activity, presence, link. Bursting five
    // lines at once overflows the USB-CDC TX buffer (the SDK sets a zero TX
    // timeout, so overflow silently drops bytes and garbles lines).
    bool        nextEvent(char* out, size_t cap);

    // ---- control ----
    void setWifi(const char* ssid, const char* pass);   // persists + reconnects
    void setSink(const char* ip, uint16_t port, uint8_t nodeId);  // persists
    void setStreaming(bool on) { _streaming = on; }
    void calibrate();
    void scanNetworks();   // list 2.4 GHz networks as log events (blocking ~3 s)

    // internal: called by the static CSI trampoline
    void _onCsi(const wifi_csi_info_t* info);

private:
    struct Slot {
        uint16_t len;
        int8_t   rssi;
        int8_t   noise;
        uint8_t  channel;
        uint8_t  nAnt;
        uint32_t seq;
        int8_t   iq[ruview_wire::MAX_IQ_BYTES];
    };

    void loadConfig();
    void startWifi();
    void enableCsi();
    void armCsi(esp_err_t* eCfg = nullptr, esp_err_t* eCb = nullptr, esp_err_t* eOn = nullptr);
    void disableCsi();
    void pumpTraffic(uint32_t nowMs);
    void startPump();
    void stopPump();
    void injectProbe();
    void drainRing(uint32_t nowMs);
    void updateRate(uint32_t nowMs);
    static void onActionStatic(const char* action, const char* value, void* ctx);
    void onAction(const char* action, const char* value);
    void logEvent(const char* msg);

    char        _name[24];
    State       _state = State::Unconfigured;
    bool        _csiOn = false;
    bool        _streaming = true;
    bool        _probeInject = false;
    bool        _promisc = false;     // promiscuous mode kills CSI on Arduino core 2.0.17 (S3); keep off
    int8_t      _probeForce = -1;     // -1 auto, 0 off, 1 on (csi_probe action)

    // config (NVS)
    Preferences _prefs;
    char        _ssid[33] = {0};
    char        _pass[65] = {0};
    char        _sinkIp[16] = "192.168.1.100";
    uint16_t    _sinkPort = ruview_wire::DEFAULT_PORT;
    uint8_t     _nodeId = 1;

    // network
    WiFiUDP     _udp;
    void*       _ping = nullptr;      // esp_ping_handle_t
    IPAddress   _sinkAddr;
    bool        _sinkValid = false;
    uint32_t    _lastPumpMs = 0;
    uint32_t    _lastProbeMs = 0;
    uint32_t    _connectStartMs = 0;

    // ring (single producer: WiFi task; single consumer: loop)
    Slot        _ring[RING_SLOTS];
    volatile uint8_t _head = 0;  // producer writes
    volatile uint8_t _tail = 0;  // consumer reads

    // statistics (written in callback)
    volatile uint32_t _framesTotal = 0;
    volatile uint32_t _dropped = 0;
    volatile int      _lastRssi = 0;
    volatile int64_t  _lastProcessUs = 0;
    uint32_t    _seq = 0;

    // rate
    uint32_t    _rateWindowStartMs = 0;
    uint32_t    _rateWindowFrames = 0;
    uint32_t    _framesAtWindowStart = 0;
    float       _rateHz = 0;

    // activity / presence
    uint32_t    _packetsSent = 0;
    uint32_t    _lastTickMs = 0;
    uint8_t     _evIdx = 0;
    uint32_t    _lastEvMs = 0;
    char        _logQ[LOGQ][LOGQ_LEN];
    uint8_t     _logHead = 0;
    uint8_t     _logCount = 0;
    uint8_t     _txBuf[ruview_wire::CSI_HEADER + ruview_wire::MAX_IQ_BYTES];
    RuViewEdge  _edge;
    uint32_t    _lastVitalsMs = 0;
    void        sendVitalsPacket(uint32_t nowMs);
};
