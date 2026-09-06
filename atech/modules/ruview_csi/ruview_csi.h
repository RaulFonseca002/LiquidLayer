/**
 * @file ruview_csi.h
 * @brief RuView-compatible WiFi CSI sensing node as an Atech module.
 *
 * Rebuild (stage-gated) design:
 *  - loop() stays short. update() only runs the WiFi state machine, polls
 *    serial actions and copies a published Snapshot. It never touches CSI
 *    frames, the DSP or UDP.
 *  - The WiFi CSI callback (WiFi task, core 0) copies frames into a lock-free
 *    ring. A dedicated DSP task (core 0, low priority) drains the ring, runs
 *    RuViewEdge, streams ADR-018 + vitals packets to the sink, and publishes
 *    results under a spinlock.
 *  - WiFi starts LATE (1.5 s after boot) so the display paints its first frame
 *    before the radio comes up.
 *  - csi_enable (NVS, default 1) lets a stage run WiFi-only.
 *
 * Ported from github.com/ruvnet/RuView firmware/esp32-csi-node (MIT):
 *   csi_collector.c (CSI config, 50 Hz early gate, ADR-018 serialization),
 *   edge_processing.c (via ruview_edge).
 */
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "ruview_wire.h"
#include "ruview_edge.h"

class RuViewCsi {
public:
    // Unconfigured: no credentials. Idle: credentials present, waiting for the
    // late start. Connected: WiFi up, CSI disabled. Streaming: WiFi up + CSI.
    enum class State : uint8_t { Unconfigured, Idle, Connecting, Connected, Streaming, Lost };

    static constexpr uint8_t  RING_SLOTS        = 16;
    static constexpr uint32_t MIN_PROCESS_US    = 20 * 1000;  // 50 Hz early gate (RuView CSI_MIN_PROCESS_INTERVAL_US)
    static constexpr uint32_t PUMP_INTERVAL_MS  = 50;         // ICMP ping to the gateway -> 20 CSI frames/s
    static constexpr uint32_t PROBE_INTERVAL_MS = 100;        // probe-request injection fallback (10 Hz)
    static constexpr float    LOW_RATE_HZ       = 12.0f;      // below this, enable probe injection
    static constexpr uint32_t LATE_START_MS     = 1500;       // let the display paint before WiFi starts
    static constexpr uint8_t  LOGQ              = 24;         // queued log lines (drained one per event tick)
    static constexpr size_t   LOGQ_LEN          = 96;

    // What the DSP task publishes and loop() reads (copied under a spinlock).
    struct Snapshot {
        float    hr = 0, br = 0, motion = 0, rateHz = 0;
        int      rssi = 0;
        bool     presence = false, fall = false, calibrating = true;
        uint32_t frames = 0, gateDrops = 0, ringDrops = 0, tx = 0;
    };

    explicit RuViewCsi(const char* instanceName);

    void begin();
    void update();

    // ---- status (loop-side, from the last snapshot) ----
    State       state() const { return _state; }
    const char* stateName() const;
    bool        isConnected() const { return _state == State::Connected || _state == State::Streaming; }
    bool        isStreaming() const { return _state == State::Streaming; }
    bool        csiEnabled() const { return _csiEnabled; }
    const Snapshot& snap() const { return _snap; }
    float       frameRateHz() const { return _snap.rateHz; }
    int         rssi() const { return _snap.rssi; }
    float       activity() const { return _snap.motion; }
    bool        presence() const { return _snap.presence; }
    bool        isCalibrating() const { return _snap.calibrating; }
    bool        vitalsCalibrating() const { return _snap.calibrating; }
    uint32_t    framesTotal() const { return _snap.frames; }
    uint32_t    packetsSent() const { return _snap.tx; }
    float       heartRate() const { return _snap.hr; }
    float       breathingRate() const { return _snap.br; }
    bool        fall() const { return _snap.fall; }
    bool        consumeBeat() { bool b = _beatPending; _beatPending = false; return b; }
    const char* sinkIp() const { return _sinkIp; }
    uint8_t     nodeId() const { return _nodeId; }
    bool        tick1Hz();
    // Paced event emitter: ONE JSON event line at most every 200 ms (bursts
    // overflow the USB-CDC TX buffer because the SDK sets a zero TX timeout).
    bool        nextEvent(char* out, size_t cap);

    // ---- control ----
    void setWifi(const char* ssid, const char* pass);          // persists + reconnects
    void setSink(const char* ip, uint16_t port, uint8_t nodeId); // persists
    void setCsiEnabled(bool on);                                // persists; applies live
    void setStreaming(bool on) { _streaming = on; }
    void calibrate() { _calibRequest = true; }
    void scanNetworks();   // list 2.4 GHz networks as log events (blocking ~3 s)

    // internal: called by the static CSI trampoline (WiFi task)
    void _onCsi(const wifi_csi_info_t* info);
    // internal: DSP task body
    void _dspLoop();

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
    void startPump();
    void stopPump();
    void injectProbe();
    void drainRing(uint32_t nowMs);
    void updateRate(uint32_t nowMs);
    void sendVitalsPacket(uint32_t nowMs);
    void publish();
    void onConnected();
    static void onActionStatic(const char* action, const char* value, void* ctx);
    void onAction(const char* action, const char* value);
    void logEvent(const char* msg);

    char        _name[24];
    State       _state = State::Unconfigured;
    bool        _csiOn = false;
    bool        _csiEnabled = true;
    bool        _streaming = true;
    bool        _probeInject = false;
    bool        _promisc = false;     // promiscuous mode kills CSI on Arduino core 2.0.17 (S3); keep off
    int8_t      _probeForce = -1;     // -1 auto, 0 off, 1 on (csi_probe action)
    uint32_t    _bootMs = 0;

    // config (NVS)
    Preferences _prefs;
    char        _ssid[33] = {0};
    char        _pass[65] = {0};
    char        _sinkIp[16] = "192.168.1.100";
    uint16_t    _sinkPort = ruview_wire::DEFAULT_PORT;
    uint8_t     _nodeId = 1;

    // network (DSP task owns _udp while running)
    WiFiUDP     _udp;
    void*       _ping = nullptr;      // esp_ping_handle_t
    IPAddress   _sinkAddr;
    bool        _sinkValid = false;
    uint32_t    _lastProbeMs = 0;
    uint32_t    _connectStartMs = 0;

    // ring (single producer: WiFi callback; single consumer: DSP task)
    Slot        _ring[RING_SLOTS];
    volatile uint8_t _head = 0;
    volatile uint8_t _tail = 0;

    // statistics written in the callback
    volatile uint32_t _framesTotal = 0;
    volatile uint32_t _gateDrops = 0;
    volatile uint32_t _ringDrops = 0;
    volatile int      _lastRssi = 0;
    volatile int64_t  _lastProcessUs = 0;
    uint32_t    _seq = 0;

    // DSP task state (task-only unless noted)
    TaskHandle_t _dspTask = nullptr;
    volatile bool _dspRun = false;
    RuViewEdge  _edge;
    uint32_t    _rateWindowStartMs = 0;
    uint32_t    _framesAtWindowStart = 0;
    float       _rateHz = 0;
    uint32_t    _packetsSent = 0;
    uint32_t    _lastVitalsMs = 0;
    uint8_t     _txBuf[ruview_wire::CSI_HEADER + ruview_wire::MAX_IQ_BYTES];
    volatile bool _calibRequest = false;   // loop -> task
    volatile bool _beatPending = false;    // task -> loop

    // published results
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    Snapshot    _pub;
    Snapshot    _snap;

    // events / logs (loop-side)
    uint32_t    _lastTickMs = 0;
    uint8_t     _evIdx = 0;
    uint32_t    _lastEvMs = 0;
    char        _logQ[LOGQ][LOGQ_LEN];
    uint8_t     _logHead = 0;
    uint8_t     _logCount = 0;
};
