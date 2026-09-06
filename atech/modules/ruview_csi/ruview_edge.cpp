/**
 * @file ruview_edge.cpp
 * @brief Amplitude-correlation CSI sensing engine (design and rationale in ruview_edge.h).
 *        Python twin: atech/host/edge_proto.py. Native tests: atech/tests/edge.
 */
#include "ruview_edge.h"
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
// One 64-entry HT-LTF block is ordered subcarrier 0..31, -32..-1. Data bins +-1..28, pilots at
// +-7 and +-21 (idx 7, 21, 43, 57), DC (0) and guards (29..35) are null.
constexpr uint8_t HT_BINS[52] = {
     1, 2, 3, 4, 5, 6, 8, 9,10,11,12,13,14,15,16,17,18,19,20,22,23,24,25,26,27,28,
    36,37,38,39,40,41,42,44,45,46,47,48,49,50,51,52,53,54,55,56,58,59,60,61,62,63 };
constexpr float ABS_ALPHA = 0.05f;
constexpr float CLAMP_K   = 5.0f;
}

// ------------------------------------------------------------------ features

bool RuViewEdge::amplitudeVector(const int8_t* iq, uint16_t iqLen, float* out, uint8_t& nOut, uint8_t& layoutOut) {
    // 256-byte (HT) and 384-byte (HT + STBC) frames carry the HT-LTF block at bytes 128..255.
    // 128-byte frames (LLTF only) are not usable: the LLTF block is noise against its own template.
    if (iqLen < 256) return false;
    layoutOut = iqLen >= 384 ? 1 : 0;
    float norm = 0;
    for (uint8_t k = 0; k < MAX_BINS; ++k) {
        uint16_t o = (uint16_t)(128 + 2u * HT_BINS[k]);
        float im = (float)iq[o], re = (float)iq[o + 1];
        out[k] = sqrtf(im * im + re * re);
        norm += out[k] * out[k];
    }
    if (norm < 1e-6f) return false;
    norm = 1.0f / sqrtf(norm);
    for (uint8_t k = 0; k < MAX_BINS; ++k) out[k] *= norm;
    nOut = MAX_BINS;
    return true;
}

float RuViewEdge::corr(const float* a, const float* b, uint8_t n) {
    float ma = 0, mb = 0;
    for (uint8_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= (float)n; mb /= (float)n;
    float sab = 0, sa = 0, sb = 0;
    for (uint8_t i = 0; i < n; ++i) {
        float da = a[i] - ma, db = b[i] - mb;
        sab += da * db; sa += da * da; sb += db * db;
    }
    float d = sqrtf(sa * sb);
    return d > 1e-12f ? sab / d : 0.0f;
}

void RuViewEdge::welfordUpdate(Welford& w, double x) {
    w.count++;
    double d = x - w.mean;
    w.mean += d / (double)w.count;
    w.m2 += d * (x - w.mean);
}

float RuViewEdge::medianOf(float* v, uint8_t n) {
    for (uint8_t i = 1; i < n; ++i) { float x = v[i]; int8_t j = (int8_t)(i - 1); while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; --j; } v[j + 1] = x; }
    return v[n / 2];
}

void RuViewEdge::designHighpass(Biquad& bq, float fs, float fc) {
    // RBJ high-pass, Q = 1/sqrt(2)
    float w0 = 2.0f * (float)M_PI * fc / fs, cw = cosf(w0), alpha = sinf(w0) / (2.0f * 0.70710678f);
    float a0inv = 1.0f / (1.0f + alpha);
    bq.b0 = (1.0f + cw) * 0.5f * a0inv; bq.b1 = -(1.0f + cw) * a0inv; bq.b2 = bq.b0;
    bq.a1 = -2.0f * cw * a0inv; bq.a2 = (1.0f - alpha) * a0inv;
    bq.x1 = bq.x2 = bq.y1 = bq.y2 = 0.0f;
}

// ------------------------------------------------------------------ lifecycle

void RuViewEdge::reset() {
    _frameCount = _layoutDrops = _untemplated = 0;
    for (uint8_t l = 0; l < LAYOUTS; ++l) {
        _havePrev[l] = false; _prevMs[l] = 0; _haveRef[l] = false; _lazyCount[l] = 0; _tplCount[l] = 0;
        for (uint8_t k = 0; k < MAX_BINS; ++k) { _prev[l][k] = _ref[l][k] = 0; _lazySum[l][k] = 0; _tplSum[l][k] = 0; }
    }
    _primary = -1; _lastMs = 0; _fs = FS; _rateMs = 0; _rateFrames = 0;
    _sj = _sw = 0; _haveS = false; _lastTemplatedMs = 0; _lastTemplatedW = 0;
    _phase = Phase::Idle; _calibrated = _openEnded = _closeRequested = false;
    _calibStartMs = _phaseStartMs = _leaveMs = 0;
    welfordReset(_statJ); welfordReset(_statW);
    _thrJ = _thrW = _offJ = _offW = 0; _meanJ = _sigJ = _meanW = _sigW = 0;
    _presence = false; _above = _below = 0; _onSinceMs = 0;
    _gridValid = false; _nextGridMs = 0;
    for (uint8_t k = 0; k < MAX_BINS; ++k) { designHighpass(_hp[k], FS, HP_FC_HZ); _absEma[k] = 0; }
    for (uint8_t f = 0; f < BR_BINS; ++f) { float w = 2.0f * (float)M_PI * (BR_F0 + BR_DF * f) / FS; _brCw[f] = cosf(w); _brSw[f] = sinf(w); }
    for (uint8_t f = 0; f < HR_BINS; ++f) { float w = 2.0f * (float)M_PI * (HR_F0 + HR_DF * f) / FS; _hrCw[f] = cosf(w); _hrSw[f] = sinf(w); }
    _blocks = 0;
    resetBlock();
    _brBpm = _brConf = _hrBpm = _hrConf = 0; _brCandidate = 0;
    _lastBeatMs = 0; _beat = false;
}

const char* RuViewEdge::phaseName() const {
    switch (_phase) { case Phase::Leave: return "leave"; case Phase::Template: return "template"; case Phase::Stats: return "stats"; default: return _calibrated ? "ready" : "idle"; }
}

uint32_t RuViewEdge::calibSecondsLeft(uint32_t nowMs) const {
    if (_phase == Phase::Idle) return 0;
    uint32_t el = nowMs - _calibStartMs;
    uint32_t minEnd = _leaveMs + TEMPLATE_MS + STATS_MIN_MS;
    uint32_t end = _openEnded ? minEnd : (CALIB_AUTO_MS > minEnd ? CALIB_AUTO_MS : minEnd);
    return el >= end ? 0 : (end - el + 999) / 1000;
}

// ------------------------------------------------------------------ calibration

void RuViewEdge::forceCalibrate(uint32_t nowMs, uint32_t leaveDelayMs, bool openEnded) {
    startCalibration(nowMs, leaveDelayMs, openEnded);
}

void RuViewEdge::startCalibration(uint32_t nowMs, uint32_t leaveDelayMs, bool openEnded) {
    _phase = leaveDelayMs ? Phase::Leave : Phase::Template;
    _openEnded = openEnded; _closeRequested = false; _leaveMs = leaveDelayMs;
    _calibStartMs = _phaseStartMs = nowMs;
    for (uint8_t l = 0; l < LAYOUTS; ++l) {
        _tplCount[l] = 0; _lazyCount[l] = 0; _haveRef[l] = false;   // wander is meaningless until the new templates exist
        for (uint8_t k = 0; k < MAX_BINS; ++k) { _tplSum[l][k] = 0; _lazySum[l][k] = 0; }
    }
    welfordReset(_statJ); welfordReset(_statW);
    _presence = false; _above = _below = 0;
    _thrJ = _thrW = _offJ = _offW = 0;
}

void RuViewEdge::updateCalibration(uint32_t nowMs, uint8_t lay, const float* a) {
    switch (_phase) {
        case Phase::Idle:
            return;
        case Phase::Leave:
            if (nowMs - _phaseStartMs >= _leaveMs) { _phase = Phase::Template; _phaseStartMs = nowMs; }
            return;
        case Phase::Template: {
            for (uint8_t k = 0; k < MAX_BINS; ++k) _tplSum[lay][k] += a[k];
            _tplCount[lay]++;
            uint32_t total = _tplCount[0] + _tplCount[1];
            if (nowMs - _phaseStartMs >= TEMPLATE_MS && total >= 20) {
                int8_t best = -1;
                for (uint8_t l = 0; l < LAYOUTS; ++l) {
                    if (_tplCount[l] < 20) continue;
                    float norm = 0;
                    for (uint8_t k = 0; k < MAX_BINS; ++k) { _ref[l][k] = (float)(_tplSum[l][k] / (double)_tplCount[l]); norm += _ref[l][k] * _ref[l][k]; }
                    norm = norm > 1e-12f ? 1.0f / sqrtf(norm) : 0.0f;
                    for (uint8_t k = 0; k < MAX_BINS; ++k) _ref[l][k] *= norm;
                    _haveRef[l] = true;
                    if (best < 0 || _tplCount[l] > _tplCount[best]) best = (int8_t)l;
                }
                if (best >= 0 && best != _primary) { _primary = best; _gridValid = false; resetBlock(); }
                _phase = Phase::Stats; _phaseStartMs = nowMs;
                _haveS = false;        // restart the EMAs so the stats are not biased by the template phase
            }
            return;
        }
        case Phase::Stats: {
            if (_haveS && _haveRef[lay]) { welfordUpdate(_statJ, _sj); welfordUpdate(_statW, _sw); }
            uint32_t inStats = nowMs - _phaseStartMs;
            uint32_t total = nowMs - _calibStartMs;
            bool done;
            if (_openEnded) done = (inStats >= STATS_MIN_MS && _closeRequested) || total >= CALIB_MAX_MS;
            else done = total >= CALIB_AUTO_MS && inStats >= STATS_MIN_MS;
            if (done) finishCalibration();
            return;
        }
    }
}

void RuViewEdge::finishCalibration() {
    _meanJ = (float)_statJ.mean; _sigJ = sqrtf((float)welfordVar(_statJ));
    _meanW = (float)_statW.mean; _sigW = sqrtf((float)welfordVar(_statW));
    _thrJ = _meanJ + K_SIGMA * _sigJ; if (_thrJ < FLOOR_J) _thrJ = FLOOR_J; if (_thrJ > CAP_THR) _thrJ = CAP_THR;
    _thrW = _meanW + K_SIGMA * _sigW; if (_thrW < FLOOR_W) _thrW = FLOOR_W; if (_thrW > CAP_THR) _thrW = CAP_THR;
    // Off levels sit between the ambient mean and the on level (mean + 2 sigma); with a degenerate
    // sigma they fall back to the midpoint so the trigger still has a dead band.
    _offJ = _meanJ + K_OFF * _sigJ; if (_offJ >= _thrJ) _offJ = 0.5f * (_meanJ + _thrJ);
    _offW = _meanW + K_OFF * _sigW; if (_offW >= _thrW) _offW = 0.5f * (_meanW + _thrW);
    _phase = Phase::Idle; _calibrated = true; _closeRequested = false;
    _presence = false; _above = _below = 0;
}

bool RuViewEdge::exportCalibration(Calibration& out) const {
    if (!_calibrated) return false;
    out.magic = CAL_MAGIC;
    memcpy(out.ref, _ref, sizeof out.ref);
    out.haveRef = templates(); out.primary = _primary;
    out.thrJ = _thrJ; out.thrW = _thrW; out.offJ = _offJ; out.offW = _offW;
    out.meanJ = _meanJ; out.sigJ = _sigJ; out.meanW = _meanW; out.sigW = _sigW;
    return true;
}

bool RuViewEdge::importCalibration(const Calibration& in) {
    if (in.magic != CAL_MAGIC || !(in.haveRef & 3) || !(in.thrJ > 0.0f) || !(in.thrW > 0.0f) || in.thrJ > CAP_THR || in.thrW > CAP_THR) return false;
    if (in.primary < 0 || in.primary >= (int8_t)LAYOUTS || !(in.haveRef & (1 << in.primary))) return false;
    memcpy(_ref, in.ref, sizeof _ref);
    for (uint8_t l = 0; l < LAYOUTS; ++l) { _haveRef[l] = (in.haveRef >> l) & 1; _lazyCount[l] = 0; }
    _primary = in.primary;
    _thrJ = in.thrJ; _thrW = in.thrW; _offJ = in.offJ; _offW = in.offW;
    _meanJ = in.meanJ; _sigJ = in.sigJ; _meanW = in.meanW; _sigW = in.sigW;
    if (!(_offJ > 0.0f) || _offJ >= _thrJ) _offJ = 0.5f * (_meanJ + _thrJ);
    if (!(_offW > 0.0f) || _offW >= _thrW) _offW = 0.5f * (_meanW + _thrW);
    _phase = Phase::Idle; _calibrated = true; _openEnded = _closeRequested = false;
    _presence = false; _above = _below = 0; _haveS = false;
    _gridValid = false; resetBlock();
    return true;
}

// ------------------------------------------------------------------ presence

void RuViewEdge::updatePresence(uint32_t nowMs) {
    bool hi = _sj > _thrJ || _sw > _thrW;
    bool lo = _sj < _offJ && _sw < _offW;
    if (!_presence) {
        _above = hi ? (uint8_t)(_above + 1) : 0;
        if (_above >= ON_FRAMES) { _presence = true; _onSinceMs = nowMs; _below = 0; _above = 0; }
    } else {
        _below = lo ? (uint8_t)(_below + 1) : 0;
        if (_below >= OFF_FRAMES && nowMs - _onSinceMs >= HOLD_MS) { _presence = false; _below = 0; }
    }
}

// ------------------------------------------------------------------ per frame

void RuViewEdge::push(const int8_t* iq, uint16_t iqLen, uint32_t nowMs) {
    float a[MAX_BINS];
    uint8_t n, lay;
    if (!amplitudeVector(iq, iqLen, a, n, lay)) { if (iqLen) _layoutDrops++; return; }
    _frameCount++;

    // sample-rate estimate (diagnostic only; vitals run on the fixed grid)
    _rateFrames++;
    if (_rateMs == 0) _rateMs = nowMs;
    else if (nowMs - _rateMs >= 1000) {
        float inst = (float)_rateFrames * 1000.0f / (float)(nowMs - _rateMs);
        _fs += 0.25f * (inst - _fs);
        _rateFrames = 0; _rateMs = nowMs;
    }

    if (!_calibrated && _phase == Phase::Idle) startCalibration(nowMs, 0, false);   // automatic boot calibration

    // Same-layout previous frame for jitter; a long gap breaks the chain.
    if (_havePrev[lay] && nowMs - _prevMs[lay] > GAP_RESET_MS) _havePrev[lay] = false;
    bool haveJ = _havePrev[lay];
    float j = haveJ ? 1.0f - corr(a, _prev[lay], n) : 0.0f; if (j < 0) j = 0;
    bool haveW = _haveRef[lay];
    float w = haveW ? 1.0f - corr(a, _ref[lay], n) : 0.0f; if (w < 0) w = 0;
    if (haveJ) {
        if (!_haveS) { _sj = j; _sw = haveW ? w : 0.0f; _haveS = true; }
        else { _sj += ALPHA * (j - _sj); if (haveW) _sw += ALPHA * (w - _sw); }
    }
    if (haveW) { _lastTemplatedMs = nowMs; _lastTemplatedW = _sw; }
    else if (_calibrated && _phase == Phase::Idle) {
        _untemplated++;
        // Wander is unknown for this layout: after 2 s without a templated frame let the smoothed value
        // decay so a stale high reading cannot hold presence; jitter still drives the decision.
        if (haveJ && nowMs - _lastTemplatedMs > 2000) _sw += ALPHA * (0.0f - _sw);
        // Lazy template for a layout unseen during calibration: only while a templated layout judged
        // the room empty and quiet within the last 2 s.
        if (!_presence && nowMs - _lastTemplatedMs <= 2000 && _lastTemplatedW < _offW && _sj < _offJ) {
            for (uint8_t k = 0; k < MAX_BINS; ++k) _lazySum[lay][k] += a[k];
            if (++_lazyCount[lay] >= LAZY_TEMPLATE_FRAMES) {
                float norm = 0;
                for (uint8_t k = 0; k < MAX_BINS; ++k) { _ref[lay][k] = (float)(_lazySum[lay][k] / (double)_lazyCount[lay]); norm += _ref[lay][k] * _ref[lay][k]; }
                norm = norm > 1e-12f ? 1.0f / sqrtf(norm) : 0.0f;
                for (uint8_t k = 0; k < MAX_BINS; ++k) _ref[lay][k] *= norm;
                _haveRef[lay] = true;
            }
        }
    }

    updateCalibration(nowMs, lay, a);
    // No presence decisions until thresholds are learned: a provisional constant is meaningless in a
    // real room (idle jitter ~0.04-0.07 here, 0.008 in the synthetic one). The screen shows "--" anyway.
    if (_phase == Phase::Idle && _calibrated && haveJ) updatePresence(nowMs);

    // Slow baseline drift while the room is empty (esp_wifi_sensing "dynamic baseline").
    if (_calibrated && _phase == Phase::Idle && !_presence && haveW && _havePrev[lay]) {
        float g = (float)(nowMs - _prevMs[lay]) / REF_TAU_MS;
        float norm = 0;
        for (uint8_t k = 0; k < MAX_BINS; ++k) { _ref[lay][k] += g * (a[k] - _ref[lay][k]); norm += _ref[lay][k] * _ref[lay][k]; }
        norm = norm > 1e-12f ? 1.0f / sqrtf(norm) : 1.0f;
        for (uint8_t k = 0; k < MAX_BINS; ++k) _ref[lay][k] *= norm;
    }

    // Vitals: uniform 20 Hz grid on the primary layout only (linear interpolation between its frames).
    if (_primary < 0 && _phase == Phase::Idle && _calibrated) _primary = (int8_t)lay;
    if ((int8_t)lay == _primary) {
        if (!_havePrev[lay]) { _gridValid = false; _blockDirty = true; }
        if (!_gridValid) {
            _gridValid = true; _nextGridMs = nowMs; _blockDirty = true;   // the (re)start transient spoils this block
            for (uint8_t k = 0; k < MAX_BINS; ++k) { _hp[k].x1 = _hp[k].x2 = a[k]; _hp[k].y1 = _hp[k].y2 = 0.0f; _absEma[k] = 0.0f; }
        } else {
            uint32_t span = nowMs - _prevMs[lay];
            float v[MAX_BINS];
            while ((int32_t)(_nextGridMs - nowMs) <= 0) {
                float f = span ? (float)(_nextGridMs - _prevMs[lay]) / (float)span : 1.0f;
                if (f < 0) f = 0;
                if (f > 1) f = 1;
                for (uint8_t k = 0; k < MAX_BINS; ++k) v[k] = _prev[lay][k] + f * (a[k] - _prev[lay][k]);
                processGridSample(v);
                _nextGridMs += GRID_MS;
            }
        }
    }

    // Heartbeat pulse for the LED: a metronome at the estimated rate while someone is present.
    if (_presence && _hrBpm > 0 && nowMs - _lastBeatMs >= (uint32_t)(60000.0f / _hrBpm)) { _lastBeatMs = nowMs; _beat = true; }

    memcpy(_prev[lay], a, sizeof(float) * n);
    _havePrev[lay] = true; _prevMs[lay] = nowMs; _lastMs = nowMs;
}

// ------------------------------------------------------------------ vitals

void RuViewEdge::resetBlock() {
    _blockN = 0; _blockDirty = false;
    memset(_brRe, 0, sizeof _brRe); memset(_brIm, 0, sizeof _brIm);
    memset(_hrRe, 0, sizeof _hrRe); memset(_hrIm, 0, sizeof _hrIm);
    for (uint8_t f = 0; f < BR_BINS; ++f) { _brC[f] = 1.0f; _brS[f] = 0.0f; }
    for (uint8_t f = 0; f < HR_BINS; ++f) { _hrC[f] = 1.0f; _hrS[f] = 0.0f; }
}

void RuViewEdge::processGridSample(const float* v) {
    float hann = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (float)_blockN / (float)(BLOCK_LEN - 1));
    for (uint8_t k = 0; k < MAX_BINS; ++k) {
        float d = runBiquad(_hp[k], v[k]);
        float ad = fabsf(d);
        if (_absEma[k] > 1e-9f && ad > CLAMP_K * _absEma[k]) d = d > 0 ? CLAMP_K * _absEma[k] : -CLAMP_K * _absEma[k];
        _absEma[k] += ABS_ALPHA * (ad - _absEma[k]);
        float x = d * hann;
        float* re = _brRe[k]; float* im = _brIm[k];
        for (uint8_t f = 0; f < BR_BINS; ++f) { re[f] += x * _brC[f]; im[f] -= x * _brS[f]; }
        re = _hrRe[k]; im = _hrIm[k];
        for (uint8_t f = 0; f < HR_BINS; ++f) { re[f] += x * _hrC[f]; im[f] -= x * _hrS[f]; }
    }
    // rotate the phasors to the next sample (renormalised to bound drift)
    for (uint8_t f = 0; f < BR_BINS; ++f) {
        float c = _brC[f] * _brCw[f] - _brS[f] * _brSw[f], s = _brS[f] * _brCw[f] + _brC[f] * _brSw[f];
        float g = 1.0f / sqrtf(c * c + s * s); _brC[f] = c * g; _brS[f] = s * g;
    }
    for (uint8_t f = 0; f < HR_BINS; ++f) {
        float c = _hrC[f] * _hrCw[f] - _hrS[f] * _hrSw[f], s = _hrS[f] * _hrCw[f] + _hrC[f] * _hrSw[f];
        float g = 1.0f / sqrtf(c * c + s * s); _hrC[f] = c * g; _hrS[f] = s * g;
    }
    if (++_blockN >= BLOCK_LEN) { finishBlock(); resetBlock(); }
}

float RuViewEdge::fusedPeak(const float* re, const float* im, uint8_t nb, float f0, float df,
                            const float* reject, uint8_t nRej, float& freqOut, uint8_t& idxOut, float* fused) {
    // re/im are [MAX_BINS][nb] row-major. Each bin's in-band spectrum is normalised to unit power
    // and summed, so a coherent peak across bins stands out while independent noise stays flat.
    for (uint8_t f = 0; f < nb; ++f) fused[f] = 0;
    for (uint8_t k = 0; k < MAX_BINS; ++k) {
        const float* r = re + (size_t)k * nb; const float* i = im + (size_t)k * nb;
        float tot = 0;
        float p[HR_BINS > BR_BINS ? HR_BINS : BR_BINS];
        for (uint8_t f = 0; f < nb; ++f) { p[f] = r[f] * r[f] + i[f] * i[f]; tot += p[f]; }
        if (tot < 1e-18f) continue;
        for (uint8_t f = 0; f < nb; ++f) fused[f] += p[f] / tot;
    }
    float tmp[HR_BINS > BR_BINS ? HR_BINS : BR_BINS];
    for (uint8_t f = 0; f < nb; ++f) tmp[f] = fused[f];
    float med = medianOf(tmp, nb);
    int best = -1; float bestV = -1;
    for (uint8_t f = 0; f < nb; ++f) {
        float fr = f0 + df * f;
        bool rej = false;
        for (uint8_t r = 0; r < nRej; ++r) if (fabsf(fr - reject[r]) <= 0.08f * reject[r]) rej = true;
        if (!rej && fused[f] > bestV) { bestV = fused[f]; best = f; }
    }
    if (best < 0 || med < 1e-12f) { freqOut = 0; idxOut = 0; return 0.0f; }
    float fpk = f0 + df * best;
    if (best > 0 && best < nb - 1) {
        float denom = fused[best - 1] - 2.0f * fused[best] + fused[best + 1];
        if (fabsf(denom) > 1e-12f) fpk += df * 0.5f * (fused[best - 1] - fused[best + 1]) / denom;
    }
    freqOut = fpk; idxOut = (uint8_t)best;
    return bestV / med;
}

void RuViewEdge::finishBlock() {
    if (_blockDirty) return;
    _blocks++;
    float fused[HR_BINS > BR_BINS ? HR_BINS : BR_BINS];
    float fBr, fHr; uint8_t iBr, iHr;
    float promBr = fusedPeak(&_brRe[0][0], &_brIm[0][0], BR_BINS, BR_F0, BR_DF, nullptr, 0, fBr, iBr, fused);
    for (uint8_t f = 0; f < BR_BINS; ++f) _lastFusedBr[f] = fused[f];
    _lastPromBr = promBr;
    bool brEdge = !(iBr > 0 && iBr < BR_BINS - 1);   // a peak on the band edge is drift, not breathing
    _brConf = brEdge ? 0.0f : (promBr - 1.0f) / 4.0f; if (_brConf > 1) _brConf = 1; if (_brConf < 0) _brConf = 0;
    bool brOk = promBr >= BR_PROMINENCE_MIN && !brEdge;
    float cand = brOk ? fBr * 60.0f : 0.0f;
    // Report only when two consecutive blocks agree: noise peaks wander from block to block.
    if (cand > 0 && _brCandidate > 0 && fabsf(cand - _brCandidate) <= BR_AGREE_BPM) _brBpm = 0.5f * (cand + _brCandidate);
    else _brBpm = 0.0f;                                 // no stale numbers
    _brCandidate = cand;
    // Heart: skip the breathing harmonics that land in the cardiac band when breathing is known.
    float rej[12]; uint8_t nRej = 0;
    if (brOk) for (uint8_t k = 2; k <= 13 && nRej < 12; ++k) { float h = fBr * k; if (h >= HR_F0 - 0.05f && h <= HR_F0 + HR_DF * HR_BINS) rej[nRej++] = h; }
    float promHr = fusedPeak(&_hrRe[0][0], &_hrIm[0][0], HR_BINS, HR_F0, HR_DF, rej, nRej, fHr, iHr, fused);
    _hrConf = (promHr - 1.0f) / 4.0f; if (_hrConf > 1) _hrConf = 1; if (_hrConf < 0) _hrConf = 0;
    _hrBpm = fHr > 0 ? fHr * 60.0f : 0.0f;
}
