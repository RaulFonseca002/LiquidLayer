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
 * 128-byte (LLTF-only) frames are ignored: the LLTF block measured as pure noise against its own
 * template (with ltf_merge on).
 *
 * Decision (candidate F of atech/analysis, chosen on labelled recordings): after a 15 s warm-up the
 * engine tracks a baseline of each feature (fast while absent and quiet, slowly otherwise so a new
 * router regime is absorbed; the wander baseline moves only while absent). A motion event is jitter > 4x its baseline in 3 of 5 frames; presence
 * is an event within 60 s, or wander > 6x its baseline while an event happened within 120 s (a still
 * person got there by moving); "moving" is an event within 5 s. Templates per frame layout bootstrap
 * from the first 300 frames of that layout and then learn only while absent and quiet. No stored
 * calibration: 30 s after boot the engine is live, and a `csi_calibrate` restarts the warm-up.
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
    // ---- presence: adaptive baseline ratios (candidate F of analysis/candidates.py)
    // The router changes its transmit mode and with it the empty-room noise level (jitter 0.005 in one
    // period, 0.8 in another); absolute thresholds do not survive that, ratios to a tracked baseline do.
    static constexpr float    ALPHA          = 0.2f;    // EMA of jitter/wander for diagnostics
    static constexpr float    R_J            = 4.0f;    // motion event: jitter > R_J x baseline
    static constexpr float    R_W            = 6.0f;    // still body: wander > R_W x baseline (with a recent event)
    static constexpr uint8_t  EV_K           = 3;       // k of the last n frames must be events
    static constexpr uint8_t  EV_N           = 5;
    static constexpr uint32_t HOLD_MS        = 60000;   // presence after the last motion event
    static constexpr uint32_t MEMORY_MS      = 120000;  // wander may keep presence this long after an event
    static constexpr uint32_t MOVING_MS      = 5000;    // "moving" = event within this window
    static constexpr float    TAU_FAST_S     = 20.0f;   // baseline tracking while absent and quiet
    static constexpr float    TAU_SLOW_S     = 900.0f;  // otherwise (absorbs a new router regime)
    static constexpr float    QUIET_RATIO    = 2.0f;
    static constexpr float    FLOOR_J        = 0.003f;
    static constexpr float    FLOOR_W        = 0.01f;    // empty rooms measure 0.003-0.02; a template built with a person
                                                        // present would otherwise learn a near-zero baseline against itself
    static constexpr float    TAU_TEMPLATE_S = 120.0f;  // template drift while absent and quiet (converges to the empty room in minutes)
    static constexpr uint16_t TEMPLATE_FRAMES = 300;    // frames that bootstrap a state's template
    static constexpr uint16_t STATE_READY    = 100;     // frames before a state's template is used for wander
    static constexpr uint8_t  STATES         = 3;       // sub-states per layout (router antenna alternation: 2 seen)
    static constexpr float    FAR_D          = 0.4f;    // 1 - corr beyond which a frame belongs to no known state
    static constexpr uint8_t  INTERLEAVE_N   = 20;      // a new state is seeded only when near and far frames interleave
    static constexpr uint8_t  INTERLEAVE_MIN = 5;
    static constexpr uint32_t WARMUP_MS      = 15000;   // no decisions; baselines = medians of the warm-up
    static constexpr uint16_t WARM_N         = 200;
    static constexpr uint32_t GAP_RESET_MS   = 1000;
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
    // Forget templates and baselines and warm up again (csi_calibrate / csi_forget).
    void restart() { reset(); }
    bool     calibrating() const { return _warm; }                 // warming up: no decisions yet
    const char* phaseName() const { return _warm ? "warmup" : "ready"; }
    uint32_t calibSecondsLeft(uint32_t nowMs) const { return (!_warm || _t0Ms == 0) ? 0 : (nowMs - _t0Ms >= WARMUP_MS ? 0 : (WARMUP_MS - (nowMs - _t0Ms) + 999) / 1000); }

    // ---- outputs
    bool     presence() const { return _presence; }
    bool     moving() const { return _moving; }                    // motion event within MOVING_MS
    float    jitter() const { return _sj; }                        // smoothed features (diagnostics)
    float    wander() const { return _sw; }
    float    baselineJitter() const { return _bj; }
    float    baselineWander() const { return _bw; }
    float    ratioJitter() const { return _rj; }                   // last frame's jitter / baseline
    float    ratioWander() const { return _rw; }
    float    thresholdJitter() const { return R_J * (_bj > FLOOR_J ? _bj : FLOOR_J); }
    float    thresholdWander() const { return R_W * (_bw > FLOOR_W ? _bw : FLOOR_W); }
    float    motionEnergy() const { return _sj; }                  // RuView wire: motion_energy
    float    presenceScore() const { return _rw; }                 // RuView wire: presence_score (>= R_W means over)
    float    heartRateBpm() const { return _hrBpm; }
    float    heartConfidence() const { return _hrConf; }
    float    breathingBpm() const { return _brBpm; }
    float    breathingConfidence() const { return _brConf; }
    bool     vitalsValid() const { return _presence && !_warm && _brBpm > 0.0f; }
    bool     fall() const { return false; }                        // disabled: never validated on hardware
    bool     consumeBeat() { bool b = _beat; _beat = false; return b; }

    // ---- diagnostics
    float    sampleRateHz() const { return _fs; }
    uint32_t frames() const { return _frameCount; }
    uint8_t  layout() const { return _primary < 0 ? 0 : (uint8_t)(_primary + 2); }   // bytes/128 of the vitals layout (2 or 3), 0 none yet
    uint8_t  templates() const { return (uint8_t)((_nStates[0] ? 1 : 0) | (_nStates[1] ? 2 : 0)); }  // bit0 256-byte, bit1 384-byte layouts have states
    uint8_t  states(uint8_t layout) const { return layout < LAYOUTS ? _nStates[layout] : 0; }        // sub-states discovered (router antenna alternation)
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
    static float  medianOf(float* v, uint16_t n);  // sorts v in place

    void resetBlock();
    void processGridSample(const float* v);
    void finishBlock();
    // Fused, per-bin-normalised spectrum over `nb` bins from the accumulators; returns prominence
    // (peak/median) and the interpolated peak frequency; `reject`/`nRej` frequencies are skipped (+-8 %).
    float fusedPeak(const float* re, const float* im, uint8_t nb, float f0, float df,
                    const float* reject, uint8_t nRej, float& freqOut, uint8_t& idxOut, float* fused);

    // ---- per-frame state
    uint32_t _frameCount = 0, _layoutDrops = 0, _untemplated = 0;
    float    _prev[LAYOUTS][STATES][MAX_BINS] = {};
    bool     _havePrev[LAYOUTS][STATES] = {};
    uint32_t _prevMs[LAYOUTS][STATES] = {};
    int8_t   assignState(uint8_t lay, const float* a, uint32_t nowMs, float& d, bool& seeded);
    void     seedState(uint8_t lay, uint8_t s, const float* a, uint32_t nowMs);
    int8_t   _primary = -1;               // layout used for vitals (most calibration frames)
    uint32_t _lastMs = 0;
    float    _fs = FS;
    uint32_t _rateMs = 0;
    uint16_t _rateFrames = 0;

    // ---- features and adaptive baselines
    float    _sj = 0, _sw = 0;            // EMA-smoothed jitter / wander (diagnostics)
    bool     _haveS = false;
    // per layout, per sub-state: template (running mean during bootstrap), previous frame, bookkeeping
    float    _ref[LAYOUTS][STATES][MAX_BINS] = {};
    double   _tplSum[LAYOUTS][STATES][MAX_BINS] = {};
    uint16_t _tplCount[LAYOUTS][STATES] = {};
    uint8_t  _nStates[LAYOUTS] = {0, 0};
    uint32_t _stateLastMs[LAYOUTS][STATES] = {};
    uint32_t _nearHist[LAYOUTS] = {0, 0}, _farHist[LAYOUTS] = {0, 0};   // INTERLEAVE_N-bit shift registers
    uint32_t _t0Ms = 0;
    bool     _warm = true;
    float    _warmJ[WARM_N] = {0}, _warmW[WARM_N] = {0};
    uint16_t _nWarmJ = 0, _nWarmW = 0;
    float    _bj = 0, _bw = 0;            // tracked baselines (0 = unknown)
    float    _rj = 0, _rw = 0;            // last ratios (0 = unknown)
    bool     _evHist[EV_N] = {false};
    uint8_t  _evPos = 0;
    uint32_t _lastEventMs = 0;
    bool     _haveEvent = false;

    // ---- presence
    bool     _presence = false;
    bool     _moving = false;
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
