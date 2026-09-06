/**
 * @file ruview_edge.h
 * @brief Single-antenna WiFi CSI sensing engine: presence, motion, breathing, best-effort heart rate.
 *
 * Feature choice (why not phase): on a single ESP32 the CSI phase carries a random per-packet
 * carrier/sampling/timing offset, so raw atan2(I,Q) is uniformly random between frames and a
 * person cannot be seen in it (measured mean |dphase| = 1.39 rad, the pi/2 noise value).
 * Espressif's own esp-radar / esp_wifi_sensing therefore work on subcarrier AMPLITUDE:
 *
 *   a_t[k]   = |CSI_t[k]| over the data bins of one LTF block, L2-normalised per frame (AGC-immune)
 *   jitter_t = 1 - corr(a_t, a_{t-1})    frame-to-frame decorrelation  -> motion
 *   wander_t = 1 - corr(a_t, a_ref)      distance to the empty-room template -> presence (still person)
 *
 * Both are EMA-smoothed; thresholds are learned as mean + K*sigma of the SMOOTHED values during an
 * empty-room calibration (template phase builds a_ref, stats phase measures the noise), then a
 * Schmitt trigger with N-frame confirmation and a hold time decides presence.
 *
 * Vitals: the normalised amplitudes are resampled onto a uniform 20 Hz grid, detrended, clamped,
 * band-passed (0.1-0.5 Hz breathing, 0.8-2.0 Hz heart), fused over the K bins with the best in-band
 * power ratio (sign-aligned to the best bin) and estimated once per second by normalised
 * autocorrelation over a 30 s window with a confidence (autocorrelation peak, 0..1). Heart rate on
 * a single 2.4 GHz antenna is best-effort and is reported together with its confidence.
 *
 * These are heuristic estimates, not medical measurements. Fall detection is disabled (never validated).
 *
 * Feed one CSI frame per push(); no heap, no RTOS calls, fixed-size state (~20 KB). The Python
 * twin is atech/host/edge_proto.py; the native tests are atech/tests/edge.
 */
#pragma once
#include <stdint.h>
#include <math.h>

class RuViewEdge {
public:
    // ---- geometry
    static constexpr uint8_t  MAX_BINS       = 52;      // HT-LTF data bins minus pilots
    static constexpr float    FS             = 20.0f;   // uniform vitals grid (Hz)
    static constexpr uint32_t GRID_MS        = 50;
    static constexpr uint16_t VIT_LEN        = 600;     // 30 s window
    static constexpr uint16_t VIT_WARMUP     = 300;     // samples before the first estimate (15 s)
    static constexpr uint8_t  VIT_K          = 5;       // bins fused for vitals
    // ---- presence
    static constexpr float    ALPHA          = 0.2f;    // EMA of jitter/wander (~250 ms at 20 Hz)
    static constexpr float    K_SIGMA        = 4.0f;    // on threshold  = mean + K*sigma
    static constexpr float    K_OFF          = 2.0f;    // off threshold = mean + K_OFF*sigma (Schmitt around the noise)
    static constexpr float    FLOOR_J        = 0.002f;
    static constexpr float    FLOOR_W        = 0.005f;
    static constexpr float    CAP_THR        = 1.5f;    // metric is 1 - corr in [0, 2]; keep thresholds reachable
    static constexpr float    DEFAULT_THR_J  = 0.20f;   // motion feedback before/while calibrating (esp-radar behaviour); real rooms idle near 0.05
    static constexpr uint8_t  ON_FRAMES      = 3;
    static constexpr uint8_t  OFF_FRAMES     = 20;
    static constexpr uint32_t HOLD_MS        = 3000;
    static constexpr float    REF_TAU_MS     = 600000.0f;  // baseline drift adaptation while absent
    static constexpr uint32_t GAP_RESET_MS   = 1000;
    // ---- calibration timing
    static constexpr uint32_t LEAVE_MS       = 10000;   // button-started calibration: time to leave the room
    static constexpr uint32_t TEMPLATE_MS    = 15000;   // builds a_ref
    static constexpr uint32_t STATS_MIN_MS   = 15000;   // minimum noise-statistics phase
    static constexpr uint32_t CALIB_AUTO_MS  = 60000;   // total for the automatic (boot) calibration
    static constexpr uint32_t CALIB_MAX_MS   = 600000;  // safety cap for an open-ended one
    // ---- vitals
    static constexpr float    BR_LO_BPM = 6.0f,  BR_HI_BPM = 30.0f;
    static constexpr float    HR_LO_BPM = 45.0f, HR_HI_BPM = 180.0f;
    static constexpr float    CONF_MIN  = 0.30f;      // autocorrelation peak needed to accept a breathing estimate
    static constexpr uint8_t  MEDIAN_N  = 5;

    enum class Phase : uint8_t { Idle, Leave, Template, Stats };

    void reset();
    // One CSI frame: raw int8 (imag, real) pairs, `iqLen` bytes, arrival time `nowMs`.
    void push(const int8_t* iq, uint16_t iqLen, uint32_t nowMs);

    // Calibration control. Automatic: started by the first frame if never calibrated (60 s).
    // Button ("empty" segment): forceCalibrate(nowMs, LEAVE_MS, true) then endCalibration() on the
    // next press; the stats phase lasts at least STATS_MIN_MS.
    void forceCalibrate(uint32_t nowMs, uint32_t leaveDelayMs = 0, bool openEnded = false);
    void endCalibration() { _closeRequested = true; }
    bool     calibrating() const { return _phase != Phase::Idle; }
    bool     calibrated() const { return _calibrated; }
    Phase    phase() const { return _phase; }
    const char* phaseName() const;
    // Seconds until the current calibration closes (0 while open-ended and past its minimum).
    uint32_t calibSecondsLeft(uint32_t nowMs) const;

    // ---- outputs
    bool     presence() const { return _presence; }
    float    jitter() const { return _sj; }                 // smoothed motion feature
    float    wander() const { return _sw; }                 // smoothed presence feature
    float    thresholdJitter() const { return _thrJ; }      // on levels
    float    thresholdWander() const { return _thrW; }
    float    offJitter() const { return _offJ; }             // off levels
    float    offWander() const { return _offW; }
    float    ambientJitter() const { return _meanJ; }        // calibration statistics
    float    ambientWander() const { return _meanW; }
    float    sigmaJitter() const { return _sigJ; }
    float    sigmaWander() const { return _sigW; }
    float    motionEnergy() const { return _sj; }           // RuView wire: motion_energy
    float    presenceScore() const { return _thrW > 0 ? _sw / _thrW : 0.0f; }  // >= 1 means over threshold
    float    heartRateBpm() const { return _hrBpm; }
    float    heartConfidence() const { return _hrConf; }
    float    breathingBpm() const { return _brBpm; }
    float    breathingConfidence() const { return _brConf; }
    bool     vitalsValid() const { return _presence && !calibrating() && _brConf >= CONF_MIN; }
    bool     fall() const { return false; }                 // disabled: never validated on hardware
    bool     consumeBeat() { bool b = _beat; _beat = false; return b; }

    // ---- diagnostics
    float    sampleRateHz() const { return _fs; }
    uint32_t frames() const { return _frameCount; }
    uint8_t  layout() const { return _layout; }             // bin set in use: 1 LLTF block (128-byte frames), 2 HT-LTF block (256/384-byte frames); 0 none yet
    uint32_t layoutDrops() const { return _layoutDrops; }
    uint16_t vitalsSamples() const { return _ringCount; }
    uint8_t  binCount() const { return _nBins; }

    // Expose the bin tables and the feature math for the host tests / Python cross-check.
    static uint8_t binsFor(uint8_t layout, const uint8_t** table);
    static bool    amplitudeVector(const int8_t* iq, uint16_t iqLen, float* out, uint8_t& nOut, uint8_t& layoutOut);
    static float   corr(const float* a, const float* b, uint8_t n);

private:
    struct Biquad { float b0, b1, b2, a1, a2, x1, x2, y1, y2; };
    struct Bandpass { Biquad s[2]; };   // two cascaded identical sections: ~24 dB/octave skirts
    struct Welford { double mean, m2; uint32_t count; };

    static void   designBandpass(Biquad& bq, float fs, float fLo, float fHi);
    static float  runBiquad(Biquad& bq, float x);
    static void   designBandpass(Bandpass& bp, float fs, float fLo, float fHi) { designBandpass(bp.s[0], fs, fLo, fHi); designBandpass(bp.s[1], fs, fLo, fHi); }
    static float  runBandpass(Bandpass& bp, float x) { return runBiquad(bp.s[1], runBiquad(bp.s[0], x)); }
    static void   welfordReset(Welford& w) { w.mean = 0; w.m2 = 0; w.count = 0; }
    static void   welfordUpdate(Welford& w, double x);
    static double welfordVar(const Welford& w) { return w.count > 1 ? w.m2 / (double)(w.count - 1) : 0.0; }
    static float  medianOf(const float* v, uint8_t n);
    // Normalised autocorrelation peak of `x[n]` for lags in [lagLo, lagHi]; returns the interpolated
    // lag (0 if none) and a confidence in `conf`. `noiseRef[lag]` is the autocorrelation the band-pass
    // filter alone produces on white noise (a narrow filter rings at its centre frequency); the peak is
    // searched on the excess over it and conf = excess / (1 - noiseRef), so noise scores ~0 and a clean
    // periodic signal ~1. `rejectLag`/`rejectN`: lags to skip (+-8 %).
    static float  autocorrPeak(const float* x, uint16_t n, uint16_t lagLo, uint16_t lagHi, const float* noiseRef,
                               const float* rejectLag, uint8_t rejectN, float& conf);
    static void   noiseAutocorr(float fs, float fLo, float fHi, uint16_t lagLo, uint16_t lagHi, float* out);

    void startCalibration(uint32_t nowMs, uint32_t leaveDelayMs, bool openEnded);
    void updateCalibration(uint32_t nowMs, const float* a);
    void finishCalibration();
    void updatePresence(uint32_t nowMs);
    void processGridSample(const float* v);
    void estimateVitals();
    void refreshTopK();

    // ---- per-frame state
    uint32_t _frameCount = 0;
    uint32_t _layoutDrops = 0;
    uint8_t  _layout = 0;
    uint8_t  _nBins = 0;
    float    _prev[MAX_BINS] = {0};
    bool     _havePrev = false;
    uint32_t _lastMs = 0;
    float    _fs = FS;
    uint32_t _rateMs = 0;
    uint16_t _rateFrames = 0;

    // ---- features
    float    _sj = 0, _sw = 0;
    bool     _haveS = false;
    float    _ref[MAX_BINS] = {0};
    bool     _haveRef = false;

    // ---- calibration
    Phase    _phase = Phase::Idle;
    bool     _calibrated = false;
    bool     _openEnded = false;
    bool     _closeRequested = false;
    uint32_t _calibStartMs = 0, _phaseStartMs = 0, _leaveMs = 0;
    double   _tplSum[MAX_BINS] = {0};
    uint32_t _tplCount = 0;
    Welford  _statJ{}, _statW{};
    float    _thrJ = DEFAULT_THR_J, _thrW = 0, _offJ = 0, _offW = 0;
    float    _meanJ = 0, _sigJ = 0, _meanW = 0, _sigW = 0;

    // ---- presence
    bool     _presence = false;
    uint8_t  _above = 0, _below = 0;
    uint32_t _onSinceMs = 0;

    // ---- vitals (uniform grid)
    bool     _gridValid = false;
    uint32_t _nextGridMs = 0;
    float    _dc[MAX_BINS] = {0};
    float    _absEma[MAX_BINS] = {0};
    Bandpass _brBq[MAX_BINS] = {}, _hrBq[MAX_BINS] = {};
    float    _pTot[MAX_BINS] = {0}, _pBr[MAX_BINS] = {0}, _pHr[MAX_BINS] = {0};
    float    _covBr[MAX_BINS] = {0}, _covHr[MAX_BINS] = {0};
    uint8_t  _topK[VIT_K] = {0};
    uint8_t  _topN = 0;
    float    _brRing[VIT_LEN] = {0}, _hrRing[VIT_LEN] = {0};
    float    _noiseBr[VIT_LEN / 2 + 1] = {0}, _noiseHr[VIT_LEN / 2 + 1] = {0};
    uint16_t _ringPos = 0, _ringCount = 0;
    uint16_t _sinceTick = 0;
    float    _brHist[MEDIAN_N] = {0}, _hrHist[MEDIAN_N] = {0};
    uint8_t  _brHistN = 0, _hrHistN = 0, _brHistPos = 0, _hrHistPos = 0;
    float    _brBpm = 0, _brConf = 0, _hrBpm = 0, _hrConf = 0;
    float    _lastHrSample = 0;
    bool     _beat = false;
};
