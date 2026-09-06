/**
 * @file ruview_edge.cpp
 * @brief Amplitude-correlation CSI sensing engine (see ruview_edge.h for the design and the
 *        reasons it replaced the RuView phase port). Python twin: atech/host/edge_proto.py.
 */
#include "ruview_edge.h"
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ------------------------------------------------------------------ bin tables
// One 64-entry LTF block is ordered subcarrier 0..31, -32..-1. Data bins: HT-LTF +-1..28,
// LLTF +-1..26; pilots at +-7 and +-21 (idx 7, 21, 43, 57); DC (0) and guards are null.
namespace {
constexpr uint8_t HT_BINS[52] = {
     1, 2, 3, 4, 5, 6, 8, 9,10,11,12,13,14,15,16,17,18,19,20,22,23,24,25,26,27,28,
    36,37,38,39,40,41,42,44,45,46,47,48,49,50,51,52,53,54,55,56,58,59,60,61,62,63 };
constexpr uint8_t LLTF_BINS[48] = {
     1, 2, 3, 4, 5, 6, 8, 9,10,11,12,13,14,15,16,17,18,19,20,22,23,24,25,26,
    38,39,40,41,42,44,45,46,47,48,49,50,51,52,53,54,55,56,58,59,60,61,62,63 };
constexpr float DC_ALPHA  = 1.0f / (4.0f * 20.0f);  // 4 s detrend at 20 Hz
constexpr float P_ALPHA   = 0.02f;                  // ~2.5 s power / covariance EMAs
constexpr float ABS_ALPHA = 0.05f;
constexpr float CLAMP_K   = 5.0f;
}

uint8_t RuViewEdge::binsFor(uint8_t layout, const uint8_t** table) {
    if (layout >= 2) { *table = HT_BINS; return 52; }
    if (layout == 1) { *table = LLTF_BINS; return 48; }
    *table = nullptr; return 0;
}

bool RuViewEdge::amplitudeVector(const int8_t* iq, uint16_t iqLen, float* out, uint8_t& nOut, uint8_t& layoutOut) {
    // 128-byte frames carry the LLTF only; 256 (HT) and 384 (HT + STBC) both carry the HT-LTF block
    // at bytes 128..255, so they share one bin set and are interchangeable frame to frame.
    uint8_t lay = (uint8_t)(iqLen / 128);
    if (lay > 2) lay = 2;
    const uint8_t* bins;
    uint8_t n = binsFor(lay, &bins);
    if (!n) return false;
    uint16_t base = lay >= 2 ? 128 : 0;   // HT-LTF block starts at byte 128
    float norm = 0;
    for (uint8_t k = 0; k < n; ++k) {
        uint16_t o = (uint16_t)(base + 2u * bins[k]);
        float im = (float)iq[o], re = (float)iq[o + 1];
        out[k] = sqrtf(im * im + re * re);
        norm += out[k] * out[k];
    }
    if (norm < 1e-6f) return false;
    norm = 1.0f / sqrtf(norm);
    for (uint8_t k = 0; k < n; ++k) out[k] *= norm;
    nOut = n; layoutOut = lay;
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

// ------------------------------------------------------------------ DSP primitives

void RuViewEdge::designBandpass(Biquad& bq, float fs, float fLo, float fHi) {
    // RBJ constant-Q bandpass, designed from the band edges: f0 = sqrt(fLo*fHi), Q = f0 / (fHi - fLo).
    float f0 = sqrtf(fLo * fHi);
    float q  = f0 / (fHi - fLo);
    float w0 = 2.0f * (float)M_PI * f0 / fs;
    float alpha = sinf(w0) / (2.0f * q);
    float a0inv = 1.0f / (1.0f + alpha);
    bq.b0 =  alpha * a0inv;
    bq.b1 =  0.0f;
    bq.b2 = -alpha * a0inv;
    bq.a1 = -2.0f * cosf(w0) * a0inv;
    bq.a2 =  (1.0f - alpha) * a0inv;
    bq.x1 = bq.x2 = bq.y1 = bq.y2 = 0.0f;
}

float RuViewEdge::runBiquad(Biquad& bq, float x) {
    float y = bq.b0 * x + bq.b1 * bq.x1 + bq.b2 * bq.x2 - bq.a1 * bq.y1 - bq.a2 * bq.y2;
    bq.x2 = bq.x1; bq.x1 = x;
    bq.y2 = bq.y1; bq.y1 = y;
    return y;
}

void RuViewEdge::welfordUpdate(Welford& w, double x) {
    w.count++;
    double d = x - w.mean;
    w.mean += d / (double)w.count;
    w.m2 += d * (x - w.mean);
}

float RuViewEdge::medianOf(const float* v, uint8_t n) {
    float tmp[MEDIAN_N];
    for (uint8_t i = 0; i < n; ++i) tmp[i] = v[i];
    for (uint8_t i = 1; i < n; ++i) { float x = tmp[i]; int8_t j = (int8_t)(i - 1); while (j >= 0 && tmp[j] > x) { tmp[j + 1] = tmp[j]; --j; } tmp[j + 1] = x; }
    return tmp[n / 2];
}

float RuViewEdge::autocorrPeak(const float* x, uint16_t n, uint16_t lagLo, uint16_t lagHi, const float* noiseRef,
                               const float* rejectLag, uint8_t rejectN, float& conf) {
    conf = 0.0f;
    if (n < 8 || lagLo < 1 || lagHi <= lagLo || lagHi >= n / 2) return 0.0f;
    float mean = 0;
    for (uint16_t i = 0; i < n; ++i) mean += x[i];
    mean /= (float)n;
    float r0 = 0;
    for (uint16_t i = 0; i < n; ++i) { float d = x[i] - mean; r0 += d * d; }
    if (r0 < 1e-9f) return 0.0f;
    r0 /= (float)n;   // unbiased per-lag normalisation below
    static float r[VIT_LEN / 2 + 1];
    float best = -2.0f;
    for (uint16_t lag = lagLo; lag <= lagHi; ++lag) {
        float s = 0;
        for (uint16_t i = 0; i + lag < n; ++i) s += (x[i] - mean) * (x[i + lag] - mean);
        r[lag] = s / ((float)(n - lag) * r0);
        if (noiseRef) r[lag] -= noiseRef[lag];    // excess over the filter's own ringing
        bool rejected = false;
        for (uint8_t k = 0; k < rejectN; ++k) if (fabsf((float)lag - rejectLag[k]) <= 0.08f * rejectLag[k]) rejected = true;
        if (rejected) r[lag] = -2.0f;
        if (r[lag] > best) best = r[lag];
    }
    if (best <= 0.0f) return 0.0f;
    // First local maximum (smallest lag) within 85 % of the best: the fundamental period.
    uint16_t pick = 0;
    for (uint16_t lag = lagLo; lag <= lagHi; ++lag) {
        if (r[lag] < 0.85f * best) continue;
        float l = lag > lagLo ? r[lag - 1] : -2.0f, h = lag < lagHi ? r[lag + 1] : -2.0f;
        if (r[lag] >= l && r[lag] >= h) { pick = lag; break; }
    }
    if (!pick) return 0.0f;
    float denomC = noiseRef ? (1.0f - noiseRef[pick]) : 1.0f;
    conf = denomC > 1e-3f ? r[pick] / denomC : 0.0f;
    if (conf > 1.0f) conf = 1.0f;
    if (conf < 0.0f) conf = 0.0f;
    // Parabolic interpolation around the peak.
    float lagF = (float)pick;
    if (pick > lagLo && pick < lagHi && r[pick - 1] > -1.5f && r[pick + 1] > -1.5f) {
        float denom = r[pick - 1] - 2.0f * r[pick] + r[pick + 1];
        if (fabsf(denom) > 1e-9f) lagF += 0.5f * (r[pick - 1] - r[pick + 1]) / denom;
    }
    return lagF;
}

void RuViewEdge::noiseAutocorr(float fs, float fLo, float fHi, uint16_t lagLo, uint16_t lagHi, float* out) {
    // Impulse response of the band-pass (long enough to decay), then its normalised autocorrelation:
    // for white-noise input the output autocorrelation equals that of the impulse response.
    static float h[VIT_LEN];
    Bandpass bp; designBandpass(bp, fs, fLo, fHi);
    float e0 = 0;
    for (uint16_t i = 0; i < VIT_LEN; ++i) { h[i] = runBandpass(bp, i == 0 ? 1.0f : 0.0f); e0 += h[i] * h[i]; }
    for (uint16_t lag = 0; lag <= VIT_LEN / 2; ++lag) out[lag] = 0;
    if (e0 < 1e-12f) return;
    for (uint16_t lag = lagLo; lag <= lagHi && lag <= VIT_LEN / 2; ++lag) {
        float s = 0;
        for (uint16_t i = 0; i + lag < VIT_LEN; ++i) s += h[i] * h[i + lag];
        out[lag] = s / e0;
    }
}

// ------------------------------------------------------------------ lifecycle

void RuViewEdge::reset() {
    _frameCount = 0; _layoutDrops = 0; _layout = 0; _nBins = 0; _havePrev = false; _lastMs = 0;
    _fs = FS; _rateMs = 0; _rateFrames = 0;
    _sj = _sw = 0; _haveS = false; _haveRef = false;
    _phase = Phase::Idle; _calibrated = false; _openEnded = false; _closeRequested = false; _leaveMs = 0;
    _calibStartMs = _phaseStartMs = 0; _tplCount = 0;
    welfordReset(_statJ); welfordReset(_statW);
    _thrJ = DEFAULT_THR_J; _thrW = 0; _offJ = 0.5f * DEFAULT_THR_J; _offW = 0;
    _meanJ = _sigJ = _meanW = _sigW = 0;
    _presence = false; _above = _below = 0; _onSinceMs = 0;
    _gridValid = false; _nextGridMs = 0;
    noiseAutocorr(FS, 0.1f, 0.5f, (uint16_t)(FS * 60.0f / BR_HI_BPM), (uint16_t)(FS * 60.0f / BR_LO_BPM), _noiseBr);
    noiseAutocorr(FS, 0.8f, 2.0f, (uint16_t)(FS * 60.0f / HR_HI_BPM), (uint16_t)(FS * 60.0f / HR_LO_BPM), _noiseHr);
    for (uint8_t k = 0; k < MAX_BINS; ++k) {
        _dc[k] = _absEma[k] = _pTot[k] = _pBr[k] = _pHr[k] = _covBr[k] = _covHr[k] = 0;
        designBandpass(_brBq[k], FS, 0.1f, 0.5f);
        designBandpass(_hrBq[k], FS, 0.8f, 2.0f);
    }
    _topN = 0; _ringPos = _ringCount = 0; _sinceTick = 0;
    _brHistN = _hrHistN = _brHistPos = _hrHistPos = 0;
    _brBpm = _brConf = _hrBpm = _hrConf = 0; _lastHrSample = 0; _beat = false;
}

const char* RuViewEdge::phaseName() const {
    switch (_phase) { case Phase::Leave: return "leave"; case Phase::Template: return "template"; case Phase::Stats: return "stats"; default: return _calibrated ? "ready" : "idle"; }
}

uint32_t RuViewEdge::calibSecondsLeft(uint32_t nowMs) const {
    if (_phase == Phase::Idle) return 0;
    uint32_t el = nowMs - _calibStartMs;
    uint32_t minEnd = _leaveMs + TEMPLATE_MS + STATS_MIN_MS;
    // open-ended (button): seconds until the minimum is met, 0 afterwards (waiting for the press)
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
    _calibStartMs = nowMs; _phaseStartMs = nowMs;
    _tplCount = 0;
    for (uint8_t k = 0; k < MAX_BINS; ++k) _tplSum[k] = 0;
    welfordReset(_statJ); welfordReset(_statW);
    _presence = false; _above = _below = 0;
    _haveRef = false;                 // wander is meaningless until the new template exists
    _thrJ = DEFAULT_THR_J; _thrW = 0; _offJ = 0.5f * DEFAULT_THR_J; _offW = 0;
}

void RuViewEdge::updateCalibration(uint32_t nowMs, const float* a) {
    switch (_phase) {
        case Phase::Idle:
            return;
        case Phase::Leave:
            if (nowMs - _phaseStartMs >= _leaveMs) { _phase = Phase::Template; _phaseStartMs = nowMs; }
            return;
        case Phase::Template:
            for (uint8_t k = 0; k < _nBins; ++k) _tplSum[k] += a[k];
            _tplCount++;
            if (nowMs - _phaseStartMs >= TEMPLATE_MS && _tplCount >= 20) {
                float norm = 0;
                for (uint8_t k = 0; k < _nBins; ++k) { _ref[k] = (float)(_tplSum[k] / (double)_tplCount); norm += _ref[k] * _ref[k]; }
                norm = norm > 1e-12f ? 1.0f / sqrtf(norm) : 0.0f;
                for (uint8_t k = 0; k < _nBins; ++k) _ref[k] *= norm;
                _haveRef = true;
                _phase = Phase::Stats; _phaseStartMs = nowMs;
                _haveS = false;        // restart the EMAs so the stats are not biased by the template phase
            }
            return;
        case Phase::Stats: {
            if (_haveS) { welfordUpdate(_statJ, _sj); welfordUpdate(_statW, _sw); }
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

// ------------------------------------------------------------------ presence

void RuViewEdge::updatePresence(uint32_t nowMs) {
    bool useW = _haveRef && _thrW > 0;
    bool hi = _sj > _thrJ || (useW && _sw > _thrW);
    bool lo = _sj < _offJ && (!useW || _sw < _offW);
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
    if (!amplitudeVector(iq, iqLen, a, n, lay)) return;
    if (_layout == 0) { _layout = lay; _nBins = n; }
    else if (lay != _layout) { _layoutDrops++; return; }

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

    if (_havePrev && nowMs - _lastMs > GAP_RESET_MS) { _havePrev = false; _gridValid = false; }
    if (!_havePrev) {
        memcpy(_prev, a, sizeof(float) * n);
        _havePrev = true; _lastMs = nowMs;
        updateCalibration(nowMs, a);
        return;
    }

    float j = 1.0f - corr(a, _prev, n); if (j < 0) j = 0;
    float w = _haveRef ? 1.0f - corr(a, _ref, n) : 0.0f; if (w < 0) w = 0;
    if (!_haveS) { _sj = j; _sw = w; _haveS = true; }
    else { _sj += ALPHA * (j - _sj); _sw += ALPHA * (w - _sw); }

    updateCalibration(nowMs, a);
    updatePresence(nowMs);

    // Slow baseline drift while the room is empty (esp_wifi_sensing "dynamic baseline").
    if (_calibrated && _phase == Phase::Idle && !_presence && _haveRef) {
        float g = (float)(nowMs - _lastMs) / REF_TAU_MS;
        float norm = 0;
        for (uint8_t k = 0; k < n; ++k) { _ref[k] += g * (a[k] - _ref[k]); norm += _ref[k] * _ref[k]; }
        norm = norm > 1e-12f ? 1.0f / sqrtf(norm) : 1.0f;
        for (uint8_t k = 0; k < n; ++k) _ref[k] *= norm;
    }

    // Uniform 20 Hz grid for the vitals filters: linear interpolation between the last two frames.
    if (!_gridValid) { _gridValid = true; _nextGridMs = nowMs; }
    uint32_t span = nowMs - _lastMs;
    float v[MAX_BINS];
    while ((int32_t)(_nextGridMs - nowMs) <= 0) {
        float f = span ? (float)(_nextGridMs - _lastMs) / (float)span : 1.0f;
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        for (uint8_t k = 0; k < n; ++k) v[k] = _prev[k] + f * (a[k] - _prev[k]);
        processGridSample(v);
        _nextGridMs += GRID_MS;
    }

    memcpy(_prev, a, sizeof(float) * n);
    _lastMs = nowMs;
}

// ------------------------------------------------------------------ vitals

void RuViewEdge::processGridSample(const float* v) {
    float br[MAX_BINS], hr[MAX_BINS];
    for (uint8_t k = 0; k < _nBins; ++k) {
        float d = v[k] - _dc[k];
        _dc[k] += DC_ALPHA * (v[k] - _dc[k]);
        float ad = fabsf(d);
        if (_absEma[k] > 1e-9f && ad > CLAMP_K * _absEma[k]) d = d > 0 ? CLAMP_K * _absEma[k] : -CLAMP_K * _absEma[k];
        _absEma[k] += ABS_ALPHA * (ad - _absEma[k]);
        br[k] = runBandpass(_brBq[k], d);
        hr[k] = runBandpass(_hrBq[k], d);
        _pTot[k] += P_ALPHA * (d * d - _pTot[k]);
        _pBr[k]  += P_ALPHA * (br[k] * br[k] - _pBr[k]);
        _pHr[k]  += P_ALPHA * (hr[k] * hr[k] - _pHr[k]);
    }
    if (_topN == 0) refreshTopK();
    uint8_t refBin = _topK[0];
    float fusedBr = 0, fusedHr = 0;
    for (uint8_t i = 0; i < _topN; ++i) {
        uint8_t k = _topK[i];
        _covBr[k] += P_ALPHA * (br[k] * br[refBin] - _covBr[k]);
        _covHr[k] += P_ALPHA * (hr[k] * hr[refBin] - _covHr[k]);
        float sBr = (k == refBin || _covBr[k] >= 0) ? 1.0f : -1.0f;
        float sHr = (k == refBin || _covHr[k] >= 0) ? 1.0f : -1.0f;
        float ratio = _pTot[k] > 1e-12f ? _pBr[k] / _pTot[k] : 0.0f;
        float wgt = sqrtf(ratio);
        fusedBr += sBr * wgt * br[k] / sqrtf(_pBr[k] + 1e-12f);
        fusedHr += sHr * wgt * hr[k] / sqrtf(_pHr[k] + 1e-12f);
    }
    _brRing[_ringPos] = fusedBr;
    _hrRing[_ringPos] = fusedHr;
    _ringPos = (uint16_t)((_ringPos + 1) % VIT_LEN);
    if (_ringCount < VIT_LEN) _ringCount++;
    if (_presence && _lastHrSample <= 0.0f && fusedHr > 0.0f) _beat = true;
    _lastHrSample = fusedHr;
    if (++_sinceTick >= 20) { _sinceTick = 0; refreshTopK(); estimateVitals(); }
}

void RuViewEdge::refreshTopK() {
    bool used[MAX_BINS] = {false};
    _topN = 0;
    for (uint8_t i = 0; i < VIT_K && i < _nBins; ++i) {
        int best = -1; float bestR = -1.0f;
        for (uint8_t k = 0; k < _nBins; ++k) {
            if (used[k]) continue;
            float r = _pTot[k] > 1e-12f ? _pBr[k] / _pTot[k] : 0.0f;
            if (r > bestR) { bestR = r; best = k; }
        }
        if (best < 0) break;
        used[best] = true;
        _topK[_topN++] = (uint8_t)best;
    }
    if (_topN == 0) { _topK[0] = 0; _topN = 1; }
}

void RuViewEdge::estimateVitals() {
    if (_ringCount < VIT_WARMUP) return;
    static float scratch[VIT_LEN];
    uint16_t len = _ringCount;
    for (uint16_t i = 0; i < len; ++i) scratch[i] = _brRing[(uint16_t)((_ringPos + VIT_LEN - len + i) % VIT_LEN)];
    float conf;
    float lag = autocorrPeak(scratch, len, (uint16_t)(FS * 60.0f / BR_HI_BPM), (uint16_t)(FS * 60.0f / BR_LO_BPM), _noiseBr, nullptr, 0, conf);
    _brConf = conf;
    if (lag > 0 && conf >= CONF_MIN) {
        float bpm = 60.0f * FS / lag;
        if (bpm >= BR_LO_BPM && bpm <= BR_HI_BPM) {
            _brHist[_brHistPos] = bpm; _brHistPos = (uint8_t)((_brHistPos + 1) % MEDIAN_N);
            if (_brHistN < MEDIAN_N) _brHistN++;
            _brBpm = medianOf(_brHist, _brHistN);
        }
    }
    // Heart: reject the breathing period and its sub-multiples (harmonics land in the cardiac band).
    for (uint16_t i = 0; i < len; ++i) scratch[i] = _hrRing[(uint16_t)((_ringPos + VIT_LEN - len + i) % VIT_LEN)];
    float rej[6]; uint8_t nRej = 0;
    if (_brBpm > 0 && _brConf >= CONF_MIN) {
        float pBr = 60.0f * FS / _brBpm;
        for (uint8_t k = 1; k <= 6; ++k) rej[nRej++] = pBr / (float)k;
    }
    float lagH = autocorrPeak(scratch, len, (uint16_t)(FS * 60.0f / HR_HI_BPM), (uint16_t)(FS * 60.0f / HR_LO_BPM), _noiseHr, rej, nRej, conf);
    _hrConf = conf;
    if (lagH > 0) {
        float bpm = 60.0f * FS / lagH;
        if (bpm >= HR_LO_BPM && bpm <= HR_HI_BPM) {
            _hrHist[_hrHistPos] = bpm; _hrHistPos = (uint8_t)((_hrHistPos + 1) % MEDIAN_N);
            if (_hrHistN < MEDIAN_N) _hrHistN++;
            _hrBpm = medianOf(_hrHist, _hrHistN);
        }
    }
}
