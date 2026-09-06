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

float RuViewEdge::medianOf(float* v, uint16_t n) {
    for (uint16_t i = 1; i < n; ++i) { float x = v[i]; int32_t j = (int32_t)i - 1; while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; --j; } v[j + 1] = x; }
    return n ? v[n / 2] : 0.0f;
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
        _nStates[l] = 0; _nearHist[l] = _farHist[l] = 0;
        for (uint8_t s = 0; s < STATES; ++s) {
            _havePrev[l][s] = false; _prevMs[l][s] = 0; _tplCount[l][s] = 0; _stateLastMs[l][s] = 0;
            for (uint8_t k = 0; k < MAX_BINS; ++k) { _prev[l][s][k] = _ref[l][s][k] = 0; _tplSum[l][s][k] = 0; }
        }
    }
    _primary = -1; _lastMs = 0; _fs = FS; _rateMs = 0; _rateFrames = 0;
    _sj = _sw = 0; _haveS = false;
    _t0Ms = 0; _warm = true; _nWarmJ = _nWarmW = 0; _bj = _bw = 0; _rj = _rw = 0;
    for (uint8_t i = 0; i < EV_N; ++i) _evHist[i] = false;
    _evPos = 0; _lastEventMs = 0; _haveEvent = false;
    _presence = false; _moving = false; _onSinceMs = 0;
    // vitals
    _gridValid = false; _nextGridMs = 0;
    for (uint8_t k = 0; k < MAX_BINS; ++k) { designHighpass(_hp[k], FS, HP_FC_HZ); _absEma[k] = 0; }
    for (uint8_t f = 0; f < BR_BINS; ++f) { float w = 2.0f * (float)M_PI * (BR_F0 + BR_DF * f) / FS; _brCw[f] = cosf(w); _brSw[f] = sinf(w); }
    for (uint8_t f = 0; f < HR_BINS; ++f) { float w = 2.0f * (float)M_PI * (HR_F0 + HR_DF * f) / FS; _hrCw[f] = cosf(w); _hrSw[f] = sinf(w); }
    _blocks = 0;
    resetBlock();
    _brBpm = _brConf = _hrBpm = _hrConf = 0; _brCandidate = 0;
    _lastBeatMs = 0; _beat = false;
}

// ------------------------------------------------------------------ router sub-states

void RuViewEdge::seedState(uint8_t lay, uint8_t s, const float* a, uint32_t nowMs) {
    for (uint8_t k = 0; k < MAX_BINS; ++k) { _tplSum[lay][s][k] = a[k]; _ref[lay][s][k] = a[k]; }
    _tplCount[lay][s] = 1; _stateLastMs[lay][s] = nowMs; _havePrev[lay][s] = false;
    if (s >= _nStates[lay]) _nStates[lay] = (uint8_t)(s + 1);
}

// Returns the state index for this frame; `d` = 1 - corr to that state's template (0 if just seeded).
int8_t RuViewEdge::assignState(uint8_t lay, const float* a, uint32_t nowMs, float& d, bool& seeded) {
    seeded = false; d = 0;
    if (_nStates[lay] == 0) { seedState(lay, 0, a, nowMs); seeded = true; return 0; }
    int8_t best = 0; float bestD = 9.0f;
    for (uint8_t s = 0; s < _nStates[lay]; ++s) {
        float dd = 1.0f - corr(a, _ref[lay][s], MAX_BINS);
        if (dd < bestD) { bestD = dd; best = (int8_t)s; }
    }
    bool far = bestD > FAR_D;
    const uint32_t mask = (INTERLEAVE_N >= 32) ? 0xFFFFFFFFu : ((1u << INTERLEAVE_N) - 1u);
    _farHist[lay] = ((_farHist[lay] << 1) | (far ? 1u : 0u)) & mask;
    _nearHist[lay] = ((_nearHist[lay] << 1) | (far ? 0u : 1u)) & mask;
    if (far) {
        uint8_t nf = (uint8_t)__builtin_popcount(_farHist[lay]), nn = (uint8_t)__builtin_popcount(_nearHist[lay]);
        if (nf >= INTERLEAVE_MIN && nn >= INTERLEAVE_MIN) {
            // near and far frames interleave: a router state, not a body. Seed a new state (free slot or LRU).
            uint8_t slot = _nStates[lay];
            if (slot >= STATES) { slot = 0; for (uint8_t s = 1; s < STATES; ++s) if (_stateLastMs[lay][s] < _stateLastMs[lay][slot]) slot = s; }
            seedState(lay, slot, a, nowMs);
            _farHist[lay] = 0; _nearHist[lay] = 0;
            seeded = true;
            return (int8_t)slot;
        }
    }
    d = bestD;
    return best;
}

// ------------------------------------------------------------------ per frame

void RuViewEdge::push(const int8_t* iq, uint16_t iqLen, uint32_t nowMs) {
    float a[MAX_BINS];
    uint8_t n, lay;
    if (!amplitudeVector(iq, iqLen, a, n, lay)) { if (iqLen) _layoutDrops++; return; }
    _frameCount++;
    if (_t0Ms == 0) _t0Ms = nowMs;

    // sample-rate estimate (diagnostic only; vitals run on the fixed grid)
    _rateFrames++;
    if (_rateMs == 0) _rateMs = nowMs;
    else if (nowMs - _rateMs >= 1000) {
        float inst = (float)_rateFrames * 1000.0f / (float)(nowMs - _rateMs);
        _fs += 0.25f * (inst - _fs);
        _rateFrames = 0; _rateMs = nowMs;
    }
    float dtS = _lastMs ? (float)(nowMs - _lastMs) / 1000.0f : 0.05f;
    if (dtS > 5.0f) { _bj = _bw = 0; }   // long gap: baselines restart from the next frames

    // which router sub-state does this frame belong to?
    float dState; bool seeded;
    int8_t st8 = assignState(lay, a, nowMs, dState, seeded);
    uint8_t st = (uint8_t)st8;
    _stateLastMs[lay][st] = nowMs;

    // jitter: previous frame of the same layout and state, within 1 s
    if (_havePrev[lay][st] && nowMs - _prevMs[lay][st] > GAP_RESET_MS) _havePrev[lay][st] = false;
    bool haveJ = _havePrev[lay][st];
    float j = haveJ ? 1.0f - corr(a, _prev[lay][st], n) : 0.0f; if (j < 0) j = 0;

    // state template: running mean over its first TEMPLATE_FRAMES near frames (far frames never enter it)
    bool nearFrame = seeded || dState <= FAR_D;
    if (nearFrame && _tplCount[lay][st] < TEMPLATE_FRAMES) {
        if (!seeded) {
            for (uint8_t k = 0; k < n; ++k) _tplSum[lay][st][k] += a[k];
            _tplCount[lay][st]++;
        }
        float norm = 0;
        for (uint8_t k = 0; k < n; ++k) { _ref[lay][st][k] = (float)(_tplSum[lay][st][k] / (double)_tplCount[lay][st]); norm += _ref[lay][st][k] * _ref[lay][st][k]; }
        norm = norm > 1e-12f ? 1.0f / sqrtf(norm) : 0.0f;
        for (uint8_t k = 0; k < n; ++k) _ref[lay][st][k] *= norm;
        if (_primary < 0 && _tplCount[lay][st] >= STATE_READY) { _primary = (int8_t)lay; _gridValid = false; resetBlock(); }
    }
    bool haveW = _tplCount[lay][st] >= STATE_READY;
    float w = 0.0f;
    if (haveW) { w = 1.0f - corr(a, _ref[lay][st], n); if (w < 0) w = 0; }
    else if (!_warm) _untemplated++;
    if (haveJ) {
        if (!_haveS) { _sj = j; _sw = haveW ? w : 0.0f; _haveS = true; }
        else { _sj += ALPHA * (j - _sj); if (haveW) _sw += ALPHA * (w - _sw); }
    }

    // warm-up: collect, learn robust baselines, decide nothing
    if (_warm) {
        if (nowMs - _t0Ms < WARMUP_MS) {
            if (haveJ && _nWarmJ < WARM_N) _warmJ[_nWarmJ++] = j;
            if (haveW && _nWarmW < WARM_N) _warmW[_nWarmW++] = w;
        } else {
            _bj = _nWarmJ ? medianOf(_warmJ, _nWarmJ) : 0; if (_bj < FLOOR_J) _bj = FLOOR_J;
            _bw = _nWarmW ? medianOf(_warmW, _nWarmW) : 0; if (_bw < FLOOR_W) _bw = FLOOR_W;
            _warm = false;
        }
    }

    if (!_warm) {
        // ratios to the tracked baselines
        float rj = 0, rw = 0;
        if (haveJ) { if (_bj <= 0) _bj = j > FLOOR_J ? j : FLOOR_J; rj = j / (_bj > FLOOR_J ? _bj : FLOOR_J); }
        if (haveW) { if (_bw <= 0) _bw = w > FLOOR_W ? w : FLOOR_W; rw = w / (_bw > FLOOR_W ? _bw : FLOOR_W); }
        _rj = haveJ ? rj : 0; _rw = haveW ? rw : 0;
        // motion event: k of the last n frames over R_J x baseline
        _evHist[_evPos] = haveJ && rj > R_J; _evPos = (uint8_t)((_evPos + 1) % EV_N);
        uint8_t cnt = 0; for (uint8_t i = 0; i < EV_N; ++i) cnt += _evHist[i] ? 1 : 0;
        if (cnt >= EV_K) { _lastEventMs = nowMs; _haveEvent = true; }
        bool moved = _haveEvent && nowMs - _lastEventMs <= HOLD_MS;
        bool remembered = _haveEvent && nowMs - _lastEventMs <= MEMORY_MS;
        bool stillBody = haveW && rw > R_W && remembered;
        bool on = moved || stillBody;
        if (on && !_presence) _onSinceMs = nowMs;
        _presence = on;
        _moving = _haveEvent && nowMs - _lastEventMs <= MOVING_MS;
        // baseline tracking: jitter fast while absent and quiet, slow otherwise (a noisier router regime
        // is absorbed within minutes); wander only while absent (a still person is never absorbed:
        // presence cannot outlive MEMORY_MS without a motion event anyway).
        bool quiet = haveJ && rj < QUIET_RATIO;
        float tau = (!_presence && quiet) ? TAU_FAST_S : TAU_SLOW_S;
        float g = dtS / tau; if (g > 1.0f) g = 1.0f;
        if (haveJ) _bj += g * (j - _bj);
        if (haveW && !_presence) _bw += g * (w - _bw);
        // template drift only while absent and quiet
        if (haveW && !_presence && quiet) {
            float gt = dtS / TAU_TEMPLATE_S; if (gt > 1.0f) gt = 1.0f;
            float norm = 0;
            for (uint8_t k = 0; k < n; ++k) { _ref[lay][st][k] += gt * (a[k] - _ref[lay][st][k]); norm += _ref[lay][st][k] * _ref[lay][st][k]; }
            norm = norm > 1e-12f ? 1.0f / sqrtf(norm) : 1.0f;
            for (uint8_t k = 0; k < n; ++k) _ref[lay][st][k] *= norm;
        }
    }

    // Vitals: uniform 20 Hz grid on the primary layout only (linear interpolation between its frames).
    if ((int8_t)lay == _primary && st == 0) {
        if (!_havePrev[lay][st]) { _gridValid = false; _blockDirty = true; }
        if (!_gridValid) {
            _gridValid = true; _nextGridMs = nowMs; _blockDirty = true;   // the (re)start transient spoils this block
            for (uint8_t k = 0; k < MAX_BINS; ++k) { _hp[k].x1 = _hp[k].x2 = a[k]; _hp[k].y1 = _hp[k].y2 = 0.0f; _absEma[k] = 0.0f; }
        } else {
            uint32_t span = nowMs - _prevMs[lay][st];
            float v[MAX_BINS];
            while ((int32_t)(_nextGridMs - nowMs) <= 0) {
                float f = span ? (float)(_nextGridMs - _prevMs[lay][st]) / (float)span : 1.0f;
                if (f < 0) f = 0;
                if (f > 1) f = 1;
                for (uint8_t k = 0; k < MAX_BINS; ++k) v[k] = _prev[lay][st][k] + f * (a[k] - _prev[lay][st][k]);
                processGridSample(v);
                _nextGridMs += GRID_MS;
            }
        }
    }

    // Heartbeat pulse for the LED: a metronome at the estimated rate while someone is present.
    if (_presence && _hrBpm > 0 && nowMs - _lastBeatMs >= (uint32_t)(60000.0f / _hrBpm)) { _lastBeatMs = nowMs; _beat = true; }

    memcpy(_prev[lay][st], a, sizeof(float) * n);
    _havePrev[lay][st] = true; _prevMs[lay][st] = nowMs; _lastMs = nowMs;
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
