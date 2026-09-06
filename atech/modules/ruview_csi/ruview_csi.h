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
 *  - USB serial: all host I/O goes through atechUsb() (atech_usb.h), which the
 *    build pipeline brings up ~2 s after boot instead of at boot (the boot-time
 *    USB-CDC init deadlocks WiFi-linked firmware intermittently; see STAGES.md).
 *  - A 40-byte NodeStatus packet goes to the sink at 1 Hz from loop(), so the
 *    host can see the main loop is alive and read state/vitals without USB.
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
        float    hrConf = 0, brConf = 0;                       // autocorrelation confidence 0..1
        float    jitter = 0, wander = 0, thrJ = 0, thrW = 0, fs = 0;   // edge features, on-thresholds, DSP sample rate
        uint32_t calibLeft = 0;                                // seconds until the calibration closes (0 = open / done)
        uint8_t  layout = 0;                                   // CSI bytes / 128 of the calibrated layout
        uint8_t  phase = 0;                                    // RuViewEdge::Phase
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
    bool        wifiEnabled() const { return _wifiEnabled; }
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
    float       heartConfidence() const { return _snap.hrConf; }
    float       breathingConfidence() const { return _snap.brConf; }
    float       jitter() const { return _snap.jitter; }
    float       wander() const { return _snap.wander; }
    float       presenceScore() const { return _snap.thrW > 0 ? _snap.wander / _snap.thrW : 0; }
    uint32_t    calibSecondsLeft() const { return _snap.calibLeft; }
    const char* calibPhaseName() const;
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
    void setWifiEnabled(bool on);                               // persists; 0 = radio never starts (stage test)
    void setStreaming(bool on) { _streaming = on; }
    void calibrate() { _calibCmd = 1; }        // relearn now (automatic 60 s window)
    // Session segments, cycled by the button: 0 live, 1 empty room (calibrating), 2 sitting still, 3 walking.
    // Entering EMPTY relearns the ambient baseline. The segment rides in NodeStatus so the host recorder
    // can label CSI frames without touching USB.
    void        setSegment(uint8_t s);
    void        nextSegment() { setSegment((uint8_t)((_segment + 1) & 3)); }
    uint8_t     segment() const { return _segment; }
    const char* segmentName() const;
    uint32_t    segmentElapsedS() const { return (millis() - _segmentStartMs) / 1000; }
    void scanNetworks();   // list 2.4 GHz networks as log events (blocking ~3 s)

    // internal: called by the static CSI trampoline (WiFi task)
    void _onCsi(const wifi_csi_info_t* info);
    // internal: DSP task body
    void _dspLoop();

private:
    struct Slot {
        uint32_t tMs;      // arrival time (ms since boot), stamped in the WiFi callback
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
    void sendStatus(uint32_t nowMs);   // 1 Hz node status from loop() (liveness beacon)
    void onConnected();
    static void onActionStatic(const char* action, const char* value, void* ctx);
    void onAction(const char* action, const char* value);
    void logEvent(const char* msg);

    char        _name[24];
    State       _state = State::Unconfigured;
    bool        _csiOn = false;
    bool        _csiEnabled = true;
    bool        _wifiEnabled = true;
    bool        _streaming = true;
    bool        _probeInject = false;
    bool        _promisc = false;     // promiscuous mode kills CSI on Arduino core 2.0.17 (S3); keep off
    int8_t      _probeForce = -1;     // -1 auto, 0 off, 1 on (csi_probe action)
    uint8_t     _segment = 0;
    uint32_t    _segmentStartMs = 0;
    volatile uint8_t _layout = 0;     // written by the DSP task
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
    WiFiUDP     _statusUdp;           // loop-side socket for the status beacon (never touched by the DSP task)
    uint32_t    _lastStatusMs = 0;
    uint32_t    _statusSeq = 0;
    uint32_t    _statusTx = 0, _statusFail = 0;
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
    volatile uint8_t _calibCmd = 0;        // loop -> task: 1 auto recalibrate, 2 button start (leave delay, open-ended), 3 end
    bool        _edgeInit = false;
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
