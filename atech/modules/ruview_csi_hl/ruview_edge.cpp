/**
 * @file ruview_edge.cpp
 * @brief Single-person Tier-2 vitals engine. Ported from RuView (MIT):
 *        github.com/ruvnet/RuView firmware/esp32-csi-node/main/edge_processing.c
 */
#include "ruview_edge.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// --------------------------------------------------------------- DSP primitives
// (verbatim from RuView edge_processing.c, adapted to member functions)

void RuViewEdge::designBandpass(Biquad& bq, float fs, float fLo, float fHi) {
    float w0 = 2.0f * (float)M_PI * (fLo + fHi) / 2.0f / fs;
    float bw = 2.0f * (float)M_PI * (fHi - fLo) / fs;
    float alpha = sinf(w0) * sinhf(logf(2.0f) / 2.0f * bw / sinf(w0));
    float a0inv = 1.0f / (1.0f + alpha);
    bq.b0 =  alpha * a0inv;
    bq.b1 =  0.0f;
    bq.b2 = -alpha * a0inv;
    bq.a1 = -2.0f * cosf(w0) * a0inv;
    bq.a2 =  (1.0f - alpha) * a0inv;
    bq.x1 = bq.x2 = bq.y1 = bq.y2 = 0.0f;
}

float RuViewEdge::processBiquad(Biquad& bq, float x) {
    float y = bq.b0 * x + bq.b1 * bq.x1 + bq.b2 * bq.x2 - bq.a1 * bq.y1 - bq.a2 * bq.y2;
    bq.x2 = bq.x1; bq.x1 = x;
    bq.y2 = bq.y1; bq.y1 = y;
    return y;
}

float RuViewEdge::extractPhase(const int8_t* iq, uint16_t idx) {
    return atan2f((float)iq[idx * 2 + 1], (float)iq[idx * 2]);
}

float RuViewEdge::unwrapPhase(float prev, float curr) {
    float diff = curr - prev;
    if (diff > (float)M_PI)  diff -= 2.0f * (float)M_PI;
    else if (diff < -(float)M_PI) diff += 2.0f * (float)M_PI;
    return prev + diff;
}

void RuViewEdge::welfordReset(Welford& w) { w.mean = 0.0; w.m2 = 0.0; w.count = 0; }

void RuViewEdge::welfordUpdate(Welford& w, double x) {
    w.count++;
    double delta = x - w.mean;
    w.mean += delta / (double)w.count;
    double delta2 = x - w.mean;
    w.m2 += delta * delta2;
}

double RuViewEdge::welfordVar(const Welford& w) {
    return (w.count > 1) ? (w.m2 / (double)(w.count - 1)) : 0.0;
}

float RuViewEdge::estimateBpm(const float* hist, uint16_t len, float fs) {
    if (len < 4) return 0.0f;
    uint16_t cross[128];
    uint16_t n = 0;
    for (uint16_t i = 1; i < len && n < 128; i++)
        if (hist[i - 1] <= 0.0f && hist[i] > 0.0f) cross[n++] = i;
    if (n < 2) return 0.0f;
    float total = 0.0f;
    for (uint16_t i = 1; i < n; i++) total += (float)(cross[i] - cross[i - 1]);
    float avg = total / (float)(n - 1);
    if (avg < 1.0f) return 0.0f;
    return (fs / avg) * 60.0f;
}

// --------------------------------------------------------------- lifecycle

void RuViewEdge::reset() {
    _pos = 0; _frameCount = 0; _haveScratch = false;
    for (auto& w : _scVar) welfordReset(w);
    _topKAge = 0; _havePrevPhase = false;
    _filtersReady = false; _designedFs = 0;
    _fs = 20.0f; _lastRateMs = 0; _rateFrames = 0;
    welfordReset(_ambient); _calibFrames = 0; _threshold = 0;
    _motion = _score = 0; _presence = false; _belowCount = 0;
    _prevAvgPhase = _prevVel = 0; _fallConsec = 0; _lastFallMs = 0; _fall = false;
    _hrBpm = _brBpm = 0; _lastBpmMs = 0; _beat = false; _lastHrSample = 0;
    for (uint8_t i = 0; i < TOP_K; ++i) _topK[i] = i;
}

void RuViewEdge::refreshTopK(uint16_t nSub) {
    // Select the TOP_K subcarriers with the highest variance (simple partial sort).
    bool used[MAX_SUBCARRIERS] = {false};
    for (uint8_t k = 0; k < TOP_K; ++k) {
        int best = -1; double bestVar = -1;
        for (uint16_t sc = 0; sc < nSub; ++sc) {
            if (used[sc]) continue;
            double v = welfordVar(_scVar[sc]);
            if (v > bestVar) { bestVar = v; best = sc; }
        }
        if (best < 0) best = k < nSub ? k : 0;
        used[best] = true;
        _topK[k] = (uint8_t)best;
    }
}

void RuViewEdge::updateSampleRate(uint32_t nowMs) {
    _rateFrames++;
    if (_lastRateMs == 0) { _lastRateMs = nowMs; return; }
    uint32_t dt = nowMs - _lastRateMs;
    if (dt >= 1000) {
        float instant = (float)_rateFrames * 1000.0f / (float)dt;
        if (instant < 8.0f) instant = 8.0f;
        if (instant > 60.0f) instant = 60.0f;
        _fs = _fs + 0.25f * (instant - _fs);   // EMA (RuView EDGE_SAMPLE_RATE_EMA_ALPHA)
        _rateFrames = 0;
        _lastRateMs = nowMs;
    }
}

void RuViewEdge::designFilters() {
    // Redesign only when the sample rate has drifted > 15 %.
    if (_filtersReady && _designedFs > 0 && fabsf(_fs - _designedFs) / _designedFs < 0.15f) return;
    designBandpass(_br, _fs, 0.1f, 0.5f);   // breathing 6-30 BPM
    designBandpass(_hr, _fs, 0.8f, 2.0f);   // heart 48-120 BPM
    _designedFs = _fs;
    _filtersReady = true;
}

// --------------------------------------------------------------- per-frame

void RuViewEdge::push(const int8_t* iq, uint16_t iqLen, uint32_t nowMs) {
    uint16_t nSub = iqLen / 2;
    if (nSub < 8) return;
    if (nSub > MAX_SUBCARRIERS) nSub = MAX_SUBCARRIERS;

    _frameCount++;
    updateSampleRate(nowMs);
    designFilters();

    // Per-subcarrier phase, unwrapped against last frame; feed variance stats.
    float motionSum = 0;
    for (uint16_t sc = 0; sc < nSub; ++sc) {
        float ph = extractPhase(iq, sc);
        if (_havePrevPhase) {
            ph = unwrapPhase(_prevPhase[sc], ph);
            motionSum += fabsf(ph - _prevPhase[sc]);
        }
        _prevPhase[sc] = ph;
        welfordUpdate(_scVar[sc], (double)ph);
    }
    if (!_havePrevPhase) { _havePrevPhase = true; return; }

    if (++_topKAge >= TOPK_REFRESH) { refreshTopK(nSub); _topKAge = 0; }

    // Aggregate phase = mean over the top-K subcarriers.
    float avg = 0; uint8_t cnt = 0;
    for (uint8_t k = 0; k < TOP_K; ++k) {
        uint8_t sc = _topK[k];
        if (sc < nSub) { avg += _prevPhase[sc]; cnt++; }
    }
    if (cnt) avg /= (float)cnt;

    // Push into the phase history ring, then the two filtered histories.
    _phase[_pos] = avg;
    float brf = processBiquad(_br, avg);
    float hrf = processBiquad(_hr, avg);
    _brHist[_pos] = brf;
    _hrHist[_pos] = hrf;

    // Heartbeat edge for the LED pulse: positive zero-crossing of the filtered heart signal.
    if (_lastHrSample <= 0.0f && hrf > 0.0f) _beat = true;
    _lastHrSample = hrf;

    _pos = (uint16_t)((_pos + 1) % PHASE_HISTORY);

    // Motion energy = mean |phase delta| this frame; EMA-smoothed.
    float motion = motionSum / (float)nSub;
    _motion = _motion * 0.9f + motion * 0.1f;
    _score = _motion;

    // Presence: 60 s ambient calibration, then Schmitt trigger on motion energy.
    if (_calibFrames < CALIB_FRAMES) {
        welfordUpdate(_ambient, (double)motion);
        _calibFrames++;
        if (_calibFrames == CALIB_FRAMES) {
            float mean = (float)_ambient.mean;
            float sigma = (float)sqrt(welfordVar(_ambient));
            _threshold = mean + CALIB_SIGMA * sigma;
            if (_threshold < 0.01f) _threshold = 0.01f;
        }
    } else {
        float high = _threshold, low = _threshold * PRESENCE_HYST;
        if (!_presence && _motion > high) { _presence = true; _belowCount = 0; }
        else if (_presence) {
            if (_motion < low) { if (++_belowCount >= PRESENCE_CLEAR) { _presence = false; _belowCount = 0; } }
            else _belowCount = 0;
        }
    }

    // Fall: phase acceleration over threshold for FALL_CONSEC frames, with cooldown.
    float vel = avg - _prevAvgPhase;
    float acc = vel - _prevVel;
    _prevAvgPhase = avg; _prevVel = vel;
    if (calibrating()) { _fallConsec = 0; _fall = false; }
    else if (fabsf(acc) * _fs * _fs > FALL_THRESH) {
        if (++_fallConsec >= FALL_CONSEC && (nowMs - _lastFallMs) > FALL_COOLDOWN_MS) {
            _fall = true; _lastFallMs = nowMs; _fallConsec = 0;
        }
    } else {
        _fallConsec = 0;
        if (_fall && (nowMs - _lastFallMs) > 1500) _fall = false;  // auto-clear the flag
    }

    // When no one is present (after calibration), fade the vitals toward 0 so a
    // stale noise reading does not freeze on screen; RuView also suppresses
    // vitals without presence.
    if (!calibrating() && !_presence) {
        _hrBpm *= 0.8f; if (_hrBpm < 1.0f) _hrBpm = 0.0f;
        _brBpm *= 0.8f; if (_brBpm < 1.0f) _brBpm = 0.0f;
    }

    // BPM once per second over the filtered histories (linearized into scratch order).
    // Only accept estimates while a body is present; noise in an empty room gives
    // meaningless but in-range numbers otherwise.
    if (_presence && nowMs - _lastBpmMs >= 1000 && _frameCount > PHASE_HISTORY / 4) {
        _lastBpmMs = nowMs;
        static float scratch[PHASE_HISTORY];
        uint16_t len = (_frameCount < PHASE_HISTORY) ? (uint16_t)_frameCount : PHASE_HISTORY;
        // breathing
        for (uint16_t i = 0; i < len; ++i) {
            uint16_t idx = (uint16_t)((_pos + PHASE_HISTORY - len + i) % PHASE_HISTORY);
            scratch[i] = _brHist[idx];
        }
        float br = estimateBpm(scratch, len, _fs);
        if (br >= 6.0f && br <= 30.0f) _brBpm = (_brBpm == 0) ? br : _brBpm * 0.7f + br * 0.3f;
        // heart
        for (uint16_t i = 0; i < len; ++i) {
            uint16_t idx = (uint16_t)((_pos + PHASE_HISTORY - len + i) % PHASE_HISTORY);
            scratch[i] = _hrHist[idx];
        }
        float hr = estimateBpm(scratch, len, _fs);
        if (hr >= 40.0f && hr <= 120.0f) _hrBpm = (_hrBpm == 0) ? hr : _hrBpm * 0.7f + hr * 0.3f;
    }
}
