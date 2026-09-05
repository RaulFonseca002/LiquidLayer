/**
 * @file ruview_edge.h
 * @brief Single-person Tier-2 vitals from CSI, ported from RuView (MIT).
 *
 * github.com/ruvnet/RuView firmware/esp32-csi-node/main/edge_processing.{c,h}:
 * biquad bandpass (RBJ constant-Q), phase extraction/unwrap, Welford variance,
 * top-K subcarrier selection, zero-crossing BPM, Welford presence with a
 * calibration window, and fall detection. This is the single-person path only;
 * multi-person clustering, delta/feature packets and WASM are dropped.
 *
 * These are heuristic estimates, not medical measurements — RuView says so too.
 *
 * Feed one CSI frame per call to push(); read heartRateBpm()/breathingBpm()/
 * presence()/fall() any time. All state is fixed-size; no heap, no RTOS calls.
 */
#pragma once
#include <stdint.h>
#include <math.h>

class RuViewEdge {
public:
    static constexpr uint16_t MAX_SUBCARRIERS = 128;  // ESP32-S3 HT20 CSI bins
    static constexpr uint16_t PHASE_HISTORY   = 256;   // ~12 s at 20 Hz
    static constexpr uint8_t  TOP_K           = 8;     // most-variant subcarriers
    static constexpr uint16_t TOPK_REFRESH    = 100;   // frames between top-K refresh
    static constexpr uint32_t CALIB_FRAMES    = 1200;  // ~60 s at 20 Hz
    static constexpr float    CALIB_SIGMA     = 3.0f;  // threshold = mean + 3 sigma
    static constexpr float    PRESENCE_HYST   = 0.5f;  // low = 0.5 * high
    static constexpr uint16_t PRESENCE_CLEAR  = 5;     // frames below low before clearing
    static constexpr float    FALL_THRESH     = 2.0f;  // rad/s^2 phase acceleration
    static constexpr uint16_t FALL_CONSEC     = 3;
    static constexpr uint32_t FALL_COOLDOWN_MS = 5000;

    void reset();
    // One CSI frame: raw int8 I/Q pairs, `iqLen` bytes, at time `nowMs`.
    void push(const int8_t* iq, uint16_t iqLen, uint32_t nowMs);

    float    heartRateBpm() const { return _hrBpm; }
    float    breathingBpm() const { return _brBpm; }
    bool     presence() const { return _presence; }
    bool     fall() const { return _fall; }
    float    motionEnergy() const { return _motion; }
    float    presenceScore() const { return _score; }
    bool     calibrating() const { return _calibFrames < CALIB_FRAMES; }
    float    sampleRateHz() const { return _fs; }
    uint32_t frames() const { return _frameCount; }
    // True once when a filtered-heart positive zero-crossing occurs (for a heartbeat LED pulse).
    bool     consumeBeat() { bool b = _beat; _beat = false; return b; }
    void     forceCalibrate() { _calibFrames = 0; welfordReset(_ambient); _threshold = 0; }

private:
    struct Biquad { float b0, b1, b2, a1, a2, x1, x2, y1, y2; };
    struct Welford { double mean, m2; uint32_t count; };

    static void  designBandpass(Biquad& bq, float fs, float fLo, float fHi);
    static float processBiquad(Biquad& bq, float x);
    static float extractPhase(const int8_t* iq, uint16_t idx);
    static float unwrapPhase(float prev, float curr);
    static void  welfordReset(Welford& w);
    static void  welfordUpdate(Welford& w, double x);
    static double welfordVar(const Welford& w);
    static float estimateBpm(const float* hist, uint16_t len, float fs);

    void refreshTopK(uint16_t nSub);
    void updateSampleRate(uint32_t nowMs);
    void designFilters();

    // aggregated phase history and the two filtered outputs (ring buffers)
    float   _phase[PHASE_HISTORY] = {0};
    float   _brHist[PHASE_HISTORY] = {0};
    float   _hrHist[PHASE_HISTORY] = {0};
    uint16_t _pos = 0;
    uint32_t _frameCount = 0;
    bool    _haveScratch = false;

    // per-subcarrier variance for top-K selection
    Welford _scVar[MAX_SUBCARRIERS];
    uint8_t _topK[TOP_K] = {0};
    uint16_t _topKAge = 0;
    float   _prevPhase[MAX_SUBCARRIERS] = {0};
    bool    _havePrevPhase = false;

    Biquad  _br{}, _hr{};
    bool    _filtersReady = false;

    // sample-rate tracking
    float    _fs = 20.0f;
    uint32_t _lastRateMs = 0;
    uint32_t _rateFrames = 0;
    float    _designedFs = 0;

    // presence / calibration
    Welford  _ambient{};
    uint32_t _calibFrames = 0;
    float    _threshold = 0;
    float    _motion = 0;
    float    _score = 0;
    bool     _presence = false;
    uint16_t _belowCount = 0;

    // fall
    float    _prevAvgPhase = 0;
    float    _prevVel = 0;
    uint16_t _fallConsec = 0;
    uint32_t _lastFallMs = 0;
    bool     _fall = false;

    // outputs
    float    _hrBpm = 0;
    float    _brBpm = 0;
    uint32_t _lastBpmMs = 0;
    bool     _beat = false;
    float    _lastHrSample = 0;
};
