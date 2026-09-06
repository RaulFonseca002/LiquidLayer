/**
 * @file ruview_edge.h
 * @brief Single-antenna WiFi CSI sensing engine: presence, motion, breathing, best-effort heart rate.
 *
 * Feature choice (why not phase): on a single ESP32 the CSI phase carries a random per-packet
 * carrier/sampling/timing offset, so raw atan2(I,Q) is uniformly random between frames and a
 * person cannot be seen in it (measured mean |dphase| = 1.39 rad, the pi/2 noise value).
 * Espressif's own esp-radar / esp_wifi_sensing therefore work on subcarrier AMPLITUDE:
 *
 *   a_t[k]   = |CSI_t[k]| over the HT-LTF data bins, L2-normalised per frame (AGC-immune)
 *   jitter_t = 1 - corr(a_t, a_{t-1})    frame-to-frame decorrelation  -> motion
 *   wander_t = 1 - corr(a_t, a_ref)      distance to the empty-room template -> presence (still person)
 *
 * Frame layouts: the access point switches between 256-byte (HT) and 384-byte (HT + STBC) frames
 * with link conditions, and their HT-LTF blocks describe different channels (measured corr -0.35),
 * so each layout keeps its own template and its own previous frame; the noise statistics are shared.
 * A layout never seen during calibration gets its template lazily, only while the room is quiet and
 * judged empty through a templated layout. 128-byte (LLTF-only) frames are ignored: the LLTF block
 * measured as pure noise against its own template.
 *
 * Both features are EMA-smoothed; thresholds are learned as mean + K*sigma of the SMOOTHED values
 * during an empty-room calibration (template phase builds a_ref, stats phase measures the noise),
 * then a Schmitt trigger (off level mean + 2 sigma) with N-frame confirmation and a hold time decides
 * presence. No presence decisions are made while calibrating.
 *
 * Vitals: the primary layout's normalised amplitudes are resampled onto a uniform 20 Hz grid,
 * detrended and clamped, and accumulated into a Hann-windowed 30 s block DFT per bin over the
 * breathing (6-30 bpm) and heart (42-150 bpm) bands (2nd-order 0.08 Hz high-pass detrend first: an EMA
 * detrend let drift dominate the lowest bins). At block end every bin's in-band spectrum is
 * normalised to unit power and summed: a coherent body signal peaks in the fused spectrum while
 * noise stays flat. Prominence = peak / median of the fused spectrum; confidence = (prominence-1)/4
 * clamped to 0..1. Breathing is reported only when two consecutive blocks agree within 1.5 bpm on a
 * peak of prominence >= 3 away from the band edges (real-room noise peaks jump between blocks);
 * heart rate is best-effort and always reported with its confidence (single-antenna 2.4 GHz CSI
 * rarely resolves it).
 *
 * These are heuristic estimates, not medical measurements. Fall detection is disabled (never validated).
 *
 * Feed one CSI frame per push(); no heap, no RTOS calls, fixed-size state (~35 KB). The Python
 * twin is atech/host/edge_proto.py; the native tests are atech/tests/edge.
 */
#pragma once
#include <stdint.h>
#include <math.h>

class RuViewEdge {
public:
    // ---- geometry
    static constexpr uint8_t  MAX_BINS       = 52;      // HT-LTF data bins minus pilots
    static constexpr uint8_t  LAYOUTS        = 2;       // 0: 256-byte HT frames, 1: 384-byte HT+STBC frames
    static constexpr float    FS             = 20.0f;   // uniform vitals grid (Hz)
    static constexpr uint32_t GRID_MS        = 50;
    static constexpr uint16_t BLOCK_LEN      = 600;     // 30 s DFT block
    // ---- presence
    static constexpr float    ALPHA          = 0.2f;    // EMA of jitter/wander (~250 ms at 20 Hz)
    static constexpr float    K_SIGMA        = 4.0f;    // on threshold  = mean + K*sigma
    static constexpr float    K_OFF          = 2.0f;    // off threshold = mean + K_OFF*sigma
    // Floors from real rooms: empty wander 0.02 +- 0.009 (p95 0.04), jitter 0.037 +- 0.013. A single very
    // quiet window learned thr_w 0.012 and then flickered on normal room noise; a still person 1-2 m
    // away gives 0.7-1.1, a person at a desk off the router-board path ~0.06.
    static constexpr float    FLOOR_J        = 0.08f;
    static constexpr float    FLOOR_W        = 0.04f;
    static constexpr float    MEAN_RATIO     = 1.75f;   // on threshold also >= MEAN_RATIO * ambient mean
    static constexpr float    CAP_THR        = 1.5f;    // metric is 1 - corr in [0, 2]; keep thresholds reachable
    static constexpr float    PLAUSIBLE_THR_W = 0.25f;  // an empty room learns thr_w ~0.04-0.06; a person in the room ~1.2
    static constexpr float    PLAUSIBLE_THR_J = 1.0f;   // jitter is only the secondary detector; mixed frame layouts inflate its sigma (0.49 seen)
    static constexpr uint8_t  ON_FRAMES      = 3;
    static constexpr uint8_t  OFF_FRAMES     = 20;
    static constexpr uint32_t HOLD_MS        = 3000;
    static constexpr float    REF_TAU_MS     = 600000.0f;  // baseline drift adaptation while absent
    static constexpr uint32_t GAP_RESET_MS   = 1000;
    static constexpr uint16_t LAZY_TEMPLATE_FRAMES = 100;  // quiet frames needed to adopt a template for a new layout
    // ---- calibration timing
    static constexpr uint32_t LEAVE_MS       = 10000;   // button-started calibration: time to leave the room
    static constexpr uint32_t TEMPLATE_MS    = 15000;   // builds a_ref
    static constexpr uint32_t STATS_MIN_MS   = 15000;   // minimum noise-statistics phase
    static constexpr uint32_t CALIB_AUTO_MS  = 60000;   // total for the automatic (boot) calibration
    static constexpr uint32_t CALIB_MAX_MS   = 600000;  // safety cap for an open-ended one
    static constexpr float    RETURN_K_SIGMA = 6.0f;    // wander above mean + 6 sigma (and above RETURN_MIN_W) during stats = someone came back
    static constexpr float    RETURN_MIN_W   = 0.05f;   // a real body gives 0.7-1.4, an empty room 0.02; primary-layout frames only
    static constexpr uint8_t  RETURN_FRAMES  = 5;
    // ---- vitals
    static constexpr uint8_t  BR_BINS = 33;   // 0.100 .. 0.500 Hz step 0.0125 (6 .. 30 bpm, 0.75 bpm)
    static constexpr uint8_t  HR_BINS = 37;   // 0.70 .. 2.50 Hz step 0.05 (42 .. 150 bpm, 3 bpm)
    static constexpr float    BR_F0 = 0.1f,  BR_DF = 0.0125f;
    static constexpr float    HR_F0 = 0.7f,  HR_DF = 0.05f;
    static constexpr float    BR_PROMINENCE_MIN = 3.0f;   // fused peak / median needed for a breathing candidate
    static constexpr float    BR_AGREE_BPM      = 1.5f;   // two consecutive blocks must agree this closely
    static constexpr float    HP_FC_HZ = 0.08f;           // 2nd-order high-pass detrend (drift and 1/f noise sit below the band)

    enum class Phase : uint8_t { Idle, Leave, Template, Stats };

    // Everything a learned calibration consists of, so it can be persisted (NVS) and restored at boot.
    struct __attribute__((packed)) Calibration {
        uint32_t magic;                 // CAL_MAGIC
        float    ref[LAYOUTS][MAX_BINS];
        uint8_t  haveRef;               // bit per layout
        int8_t   primary;
        float    thrJ, thrW, offJ, offW, meanJ, sigJ, meanW, sigW;
    };
    static constexpr uint32_t CAL_MAGIC = 0xCA11B002u;
    bool exportCalibration(Calibration& out) const;   // false if not calibrated
    bool importCalibration(const Calibration& in);    // validates and makes the engine calibrated

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
    // False when the learned thresholds say the room was not empty during calibration (do not persist).
    bool     calibrationPlausible() const { return _calibrated && _thrW <= PLAUSIBLE_THR_W && _thrJ <= PLAUSIBLE_THR_J; }
    bool     calibrationClosedByReturn() const { return _closedByReturn; }   // the last calibration ended because someone re-entered
    Phase    phase() const { return _phase; }
    const char* phaseName() const;
    uint32_t calibSecondsLeft(uint32_t nowMs) const;   // 0 while open-ended past its minimum, or done

    // ---- outputs
    bool     presence() const { return _presence; }
    float    jitter() const { return _sj; }
    float    wander() const { return _sw; }
    float    thresholdJitter() const { return _thrJ; }
    float    thresholdWander() const { return _thrW; }
    float    offJitter() const { return _offJ; }
    float    offWander() const { return _offW; }
    float    ambientJitter() const { return _meanJ; }
    float    ambientWander() const { return _meanW; }
    float    sigmaJitter() const { return _sigJ; }
    float    sigmaWander() const { return _sigW; }
    float    motionEnergy() const { return _sj; }
    float    presenceScore() const { return _thrW > 0 ? _sw / _thrW : 0.0f; }  // >= 1 means over threshold
    float    heartRateBpm() const { return _hrBpm; }
    float    heartConfidence() const { return _hrConf; }
    float    breathingBpm() const { return _brBpm; }          // 0 until a prominent, in-band peak was seen
    float    breathingConfidence() const { return _brConf; }  // (prominence - 1) / 4, clamped 0..1
    bool     vitalsValid() const { return _presence && !calibrating() && _brBpm > 0.0f; }
    bool     fall() const { return false; }                   // disabled: never validated on hardware
    bool     consumeBeat() { bool b = _beat; _beat = false; return b; }

    // ---- diagnostics
    float    sampleRateHz() const { return _fs; }
    uint32_t frames() const { return _frameCount; }
    uint8_t  layout() const { return _primary < 0 ? 0 : (uint8_t)(_primary + 2); }   // bytes/128 of the vitals layout (2 or 3), 0 none yet
    uint8_t  templates() const { return (uint8_t)((_haveRef[0] ? 1 : 0) | (_haveRef[1] ? 2 : 0)); }  // bit0 256-byte, bit1 384-byte
    uint32_t layoutDrops() const { return _layoutDrops; }     // frames of unsupported layouts (128-byte LLTF-only)
    uint32_t untemplated() const { return _untemplated; }     // frames whose layout had no template yet
    uint16_t vitalsSamples() const { return _blockN; }
    uint8_t  binCount() const { return MAX_BINS; }
    uint32_t blocks() const { return _blocks; }
    const float* lastFusedBreathing() const { return _lastFusedBr; }   // BR_BINS values from the last finished block
    float    lastBreathingProminence() const { return _lastPromBr; }

    // Exposed for the host tests / Python cross-check.
    static bool  amplitudeVector(const int8_t* iq, uint16_t iqLen, float* out, uint8_t& nOut, uint8_t& layoutOut);
    static float corr(const float* a, const float* b, uint8_t n);

private:
    struct Welford { double mean, m2; uint32_t count; };
    static void   welfordReset(Welford& w) { w.mean = 0; w.m2 = 0; w.count = 0; }
    static void   welfordUpdate(Welford& w, double x);
    static double welfordVar(const Welford& w) { return w.count > 1 ? w.m2 / (double)(w.count - 1) : 0.0; }
    static float  medianOf(float* v, uint8_t n);   // sorts v in place

    void startCalibration(uint32_t nowMs, uint32_t leaveDelayMs, bool openEnded);
    void updateCalibration(uint32_t nowMs, uint8_t lay, const float* a);
    void finishCalibration();
    void updatePresence(uint32_t nowMs);
    void resetBlock();
    void processGridSample(const float* v);
    void finishBlock();
    // Fused, per-bin-normalised spectrum over `nb` bins from the accumulators; returns prominence
    // (peak/median) and the interpolated peak frequency; `reject`/`nRej` frequencies are skipped (+-8 %).
    float fusedPeak(const float* re, const float* im, uint8_t nb, float f0, float df,
                    const float* reject, uint8_t nRej, float& freqOut, uint8_t& idxOut, float* fused);

    // ---- per-frame state
    uint32_t _frameCount = 0, _layoutDrops = 0, _untemplated = 0;
    float    _prev[LAYOUTS][MAX_BINS] = {};
    bool     _havePrev[LAYOUTS] = {false, false};
    uint32_t _prevMs[LAYOUTS] = {0, 0};
    int8_t   _primary = -1;               // layout used for vitals (most calibration frames)
    uint32_t _lastMs = 0;
    float    _fs = FS;
    uint32_t _rateMs = 0;
    uint16_t _rateFrames = 0;

    // ---- features
    float    _sj = 0, _sw = 0;
    bool     _haveS = false;
    float    _ref[LAYOUTS][MAX_BINS] = {};
    bool     _haveRef[LAYOUTS] = {false, false};
    uint32_t _lastTemplatedMs = 0;        // last frame judged through a templated layout
    float    _lastTemplatedW = 0;
    double   _lazySum[LAYOUTS][MAX_BINS] = {};
    uint16_t _lazyCount[LAYOUTS] = {0, 0};

    // ---- calibration
    Phase    _phase = Phase::Idle;
    bool     _calibrated = false, _openEnded = false, _closeRequested = false;
    uint32_t _calibStartMs = 0, _phaseStartMs = 0, _leaveMs = 0;
    uint8_t  _returnFrames = 0;
    bool     _closedByReturn = false;
    double   _tplSum[LAYOUTS][MAX_BINS] = {};
    uint32_t _tplCount[LAYOUTS] = {0, 0};
    Welford  _statJ{}, _statW{};
    float    _thrJ = 0, _thrW = 0, _offJ = 0, _offW = 0;
    float    _meanJ = 0, _sigJ = 0, _meanW = 0, _sigW = 0;

    // ---- presence
    bool     _presence = false;
    uint8_t  _above = 0, _below = 0;
    uint32_t _onSinceMs = 0;

    // ---- vitals: uniform grid + block DFT accumulators
    bool     _gridValid = false;
    uint32_t _nextGridMs = 0;
    struct Biquad { float b0, b1, b2, a1, a2, x1, x2, y1, y2; };
    static void  designHighpass(Biquad& bq, float fs, float fc);
    static float runBiquad(Biquad& bq, float x) { float y = bq.b0 * x + bq.b1 * bq.x1 + bq.b2 * bq.x2 - bq.a1 * bq.y1 - bq.a2 * bq.y2; bq.x2 = bq.x1; bq.x1 = x; bq.y2 = bq.y1; bq.y1 = y; return y; }
    Biquad   _hp[MAX_BINS] = {};
    float    _absEma[MAX_BINS] = {0};
    uint16_t _blockN = 0;
    bool     _blockDirty = false;         // gap or layout change inside the block: discard at the end
    uint32_t _blocks = 0;
    float    _brRe[MAX_BINS][BR_BINS] = {}, _brIm[MAX_BINS][BR_BINS] = {};
    float    _hrRe[MAX_BINS][HR_BINS] = {}, _hrIm[MAX_BINS][HR_BINS] = {};
    float    _brC[BR_BINS] = {0}, _brS[BR_BINS] = {0};   // rotating phasors (cos, sin) per frequency
    float    _hrC[HR_BINS] = {0}, _hrS[HR_BINS] = {0};
    float    _brCw[BR_BINS] = {0}, _brSw[BR_BINS] = {0}; // per-sample rotation
    float    _hrCw[HR_BINS] = {0}, _hrSw[HR_BINS] = {0};
    float    _brBpm = 0, _brConf = 0, _hrBpm = 0, _hrConf = 0;
    float    _brCandidate = 0;            // last block's in-band candidate (bpm), 0 if none
    float    _lastFusedBr[BR_BINS] = {0}; float _lastPromBr = 0;
    uint32_t _lastBeatMs = 0;
    bool     _beat = false;
};
