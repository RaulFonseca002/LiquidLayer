// test_ruview_edge.cpp — native tests for RuViewEdge (no Arduino, no SDK). Built by run.sh.
//
//   test_ruview_edge                      synthetic scenarios (exit 1 on failure)
//   test_ruview_edge REC.csirec [...]     replay recordings: per-segment presence/vitals report
//   test_ruview_edge --assert REC.csirec  replay + acceptance gates (empty <1 %, still >90 %, walk >95 %)
//   test_ruview_edge --dump REC.csirec OUT.tsv   per-frame t seg jitter wander presence (cross-check with edge_proto.py)
#include "ruview_edge.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <random>

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// ------------------------------------------------------------------ synthetic CSI
struct Room {
    std::mt19937 rng{42};
    float base[64];            // HT-LTF block amplitudes for the 64 bins
    float delta[64] = {0};     // multiplicative channel change caused by a person
    float phase[64] = {0};
    float breathAmp = 0.0f, breathHz = 0.25f;
    float gain = 1.0f;         // AGC step
    float noise = 1.0f;        // I/Q noise sigma (LSB)
    Room() {
        std::uniform_real_distribution<float> u(20.0f, 60.0f), ph(0.0f, 6.283f);
        for (int k = 0; k < 64; ++k) { base[k] = u(rng); phase[k] = ph(rng); }
    }
    void personStill(float strength) {   // channel changed by a body; breathing modulates some bins
        std::uniform_real_distribution<float> u(-strength, strength);
        for (int k = 0; k < 64; ++k) delta[k] = u(rng);
        breathAmp = 0.05f;
    }
    void personWalk() { std::uniform_real_distribution<float> u(-0.3f, 0.3f); for (int k = 0; k < 64; ++k) delta[k] = u(rng); breathAmp = 0; }
    void empty() { for (int k = 0; k < 64; ++k) delta[k] = 0; breathAmp = 0; }
    // 384-byte STBC HT20 frame: LLTF (zeros), HT-LTF (signal), STBC-HT-LTF (zeros)
    void frame(float tS, int8_t* out) {
        std::normal_distribution<float> n(0.0f, noise);
        std::uniform_real_distribution<float> ph(0.0f, 6.283f);
        std::memset(out, 0, 384);
        float common = ph(rng);   // random per-packet phase offset (CFO), as on real hardware
        for (int k = 0; k < 64; ++k) {
            float a = base[k] * (1.0f + delta[k]) * gain;
            bool breathing = breathAmp > 0 && (k % 7 == 3);   // a few bins see the chest
            if (breathing) a *= 1.0f + breathAmp * std::sin(6.283f * breathHz * tS + phase[k]);
            if (k == 0 || (k >= 29 && k <= 35)) a = 0;        // DC and guards
            float p = common + phase[k];
            float im = a * std::sin(p) + n(rng), re = a * std::cos(p) + n(rng);
            im = std::fmax(-127.f, std::fmin(127.f, im)); re = std::fmax(-127.f, std::fmin(127.f, re));
            out[128 + 2 * k] = (int8_t)std::lrint(im);
            out[128 + 2 * k + 1] = (int8_t)std::lrint(re);
        }
    }
};

// Run `seconds` at ~20 Hz with a little timing jitter; returns the fraction of frames with presence.
static float run(RuViewEdge& e, Room& room, float& tS, float seconds, uint32_t& nowMs, float* brOut = nullptr, float* brConf = nullptr) {
    std::mt19937 jrng(7);
    std::uniform_int_distribution<int> jit(-8, 8);
    int frames = 0, on = 0;
    float tEnd = tS + seconds;
    int8_t buf[384];
    while (tS < tEnd) {
        room.frame(tS, buf);
        e.push(buf, 384, nowMs);
        frames++; on += e.presence() ? 1 : 0;
        int dt = 50 + jit(jrng);
        tS += dt / 1000.0f; nowMs += (uint32_t)dt;
    }
    if (brOut) *brOut = e.breathingBpm();
    if (brConf) *brConf = e.breathingConfidence();
    return frames ? (float)on / (float)frames : 0.0f;
}

static void testSynthetic() {
    std::printf("synthetic scenario\n");
    RuViewEdge e; e.reset();
    Room room; float t = 0; uint32_t now = 1000;
    // (a) empty room, automatic 60 s calibration, then 30 s empty
    run(e, room, t, 62.0f, now);
    CHECK(e.calibrated(), "calibrated after 62 s (phase %s)", e.phaseName());
    CHECK(!e.calibrating(), "not calibrating after auto window");
    float thrJ = e.thresholdJitter(), thrW = e.thresholdWander();
    std::printf("  thresholds: jitter on %.4f off %.4f  wander on %.4f off %.4f (ambient j %.4f+-%.4f w %.4f+-%.4f)\n", thrJ, e.offJitter(), thrW, e.offWander(), e.ambientJitter(), e.sigmaJitter(), e.ambientWander(), e.sigmaWander());
    CHECK(thrJ > 0 && thrJ < 1.0f && thrW > 0 && thrW < 1.0f, "thresholds finite and reachable");
    float fEmpty = run(e, room, t, 30.0f, now);
    std::printf("  empty:  presence %.1f%%\n", 100 * fEmpty);
    CHECK(fEmpty < 0.01f, "empty room stays absent (%.1f%%)", 100 * fEmpty);
    // (b) still person with breathing 15 BPM
    room.personStill(0.15f);
    float brBpm = 0, brC = 0;
    float fStill = run(e, room, t, 60.0f, now, &brBpm, &brC);
    std::printf("  still:  presence %.1f%%  wander %.4f  BR %.1f bpm conf %.2f  HR %.1f conf %.2f  bins %u\n",
                100 * fStill, e.wander(), brBpm, brC, e.heartRateBpm(), e.heartConfidence(), e.binCount());
    CHECK(fStill > 0.95f, "still person detected (%.1f%%)", 100 * fStill);
    CHECK(std::fabs(brBpm - 15.0f) <= 1.5f, "breathing 15 +- 1.5 bpm (got %.1f)", brBpm);
    CHECK(brC >= RuViewEdge::CONF_MIN, "breathing confidence >= %.2f (got %.2f)", RuViewEdge::CONF_MIN, brC);
    CHECK(!e.fall(), "fall disabled");
    // (c) walking
    room.personWalk();
    float fWalk = 0; { int frames = 0, on = 0; float tEnd = t + 20; int8_t buf[384];
        while (t < tEnd) { if (((int)(t * 2)) % 1 == 0 && std::fmod(t, 0.5f) < 0.06f) room.personWalk(); room.frame(t, buf); e.push(buf, 384, now); frames++; on += e.presence(); t += 0.05f; now += 50; }
        fWalk = (float)on / frames; }
    std::printf("  walk:   presence %.1f%%  jitter %.4f\n", 100 * fWalk, e.jitter());
    CHECK(fWalk > 0.95f, "walking detected (%.1f%%)", 100 * fWalk);
    // (d) leaves: presence must drop within 5 s
    room.empty();
    float tLeft = t; bool off = false; { int8_t buf[384]; while (t < tLeft + 10) { room.frame(t, buf); e.push(buf, 384, now); if (!e.presence() && !off) { off = true; std::printf("  leave:  off after %.1f s\n", t - tLeft); } t += 0.05f; now += 50; } }
    CHECK(off && !e.presence(), "presence clears after the person leaves");
    float fAfter = run(e, room, t, 20.0f, now);
    CHECK(fAfter < 0.02f, "stays absent after leaving (%.1f%%)", 100 * fAfter);
    // (e) AGC gain step in an empty room must not trigger presence
    room.gain = 1.5f;
    float fAgc = run(e, room, t, 20.0f, now);
    std::printf("  agc x1.5 empty: presence %.1f%%\n", 100 * fAgc);
    CHECK(fAgc < 0.02f, "gain step does not fake presence (%.1f%%)", 100 * fAgc);
    room.gain = 1.0f;
    // (f) button calibration: leave delay, open-ended, closes on request after the minimum
    room.personStill(0.15f); run(e, room, t, 5.0f, now);
    CHECK(e.presence(), "person present before recalibration");
    e.forceCalibrate(now, RuViewEdge::LEAVE_MS, true);
    CHECK(!e.presence() && e.calibrating() && e.phase() == RuViewEdge::Phase::Leave, "forceCalibrate clears presence and enters leave phase");
    room.empty();
    run(e, room, t, 5.0f, now);
    CHECK(e.phase() == RuViewEdge::Phase::Leave, "still leaving at 5 s");
    run(e, room, t, 30.0f, now);   // 35 s in: leave 10 + template 15 + 10 of the 15 s minimum stats
    CHECK(e.calibrating() && e.phase() == RuViewEdge::Phase::Stats, "open-ended calibration keeps collecting (phase %s)", e.phaseName());
    CHECK(e.calibSecondsLeft(now) >= 4 && e.calibSecondsLeft(now) <= 6, "countdown ~5 s (got %lu)", (unsigned long)e.calibSecondsLeft(now));
    run(e, room, t, 6.0f, now);
    CHECK(e.calibSecondsLeft(now) == 0, "minimum met: countdown 0");
    run(e, room, t, 20.0f, now);
    CHECK(e.calibrating(), "open-ended stays open without the button");
    e.endCalibration();
    run(e, room, t, 1.0f, now);
    CHECK(!e.calibrating() && e.calibrated(), "closes on request");
    room.personStill(0.15f);
    float fAgain = run(e, room, t, 20.0f, now);
    CHECK(fAgain > 0.9f, "detects again after recalibration (%.1f%%)", 100 * fAgain);
    // (g) frames of another layout are counted and dropped, not processed
    int8_t small[128]; for (int i = 0; i < 128; ++i) small[i] = (int8_t)(10 + (i % 7)); uint32_t drops = e.layoutDrops();
    e.push(small, 128, now + 50);
    CHECK(e.layoutDrops() == drops + 1, "layout mismatch counted");
    // a 256-byte (non-STBC HT) frame shares the HT-LTF bin set with the 384-byte ones: processed, not dropped
    { int8_t ht[256]; room.frame(t, small); (void)small; std::memcpy(ht, small, 0); int8_t full[384]; room.frame(t, full); std::memcpy(ht, full, 256);
      uint32_t fr = e.frames(); e.push(ht, 256, now + 100); CHECK(e.frames() == fr + 1 && e.layoutDrops() == drops + 1, "256-byte HT frame accepted"); }
    // (h) gap: a 3 s hole resets the frame chain without breaking calibration
    now += 3000; t += 3.0f;
    run(e, room, t, 5.0f, now);
    CHECK(e.calibrated() && e.presence(), "survives a gap");
}

static void testRandomPhase() {
    std::printf("random phase, constant amplitude (the old failure mode)\n");
    RuViewEdge e; e.reset();
    Room room; room.noise = 0.5f; float t = 0; uint32_t now = 500;
    run(e, room, t, 70.0f, now);
    std::printf("  jitter %.5f (thr %.5f)  wander %.5f (thr %.5f)  noise-only HR %.0f conf %.2f  BR %.0f conf %.2f\n", e.jitter(), e.thresholdJitter(),
                e.wander(), e.thresholdWander(), e.heartRateBpm(), e.heartConfidence(), e.breathingBpm(), e.breathingConfidence());
    CHECK(e.jitter() < 0.05f, "random phase does not create motion (jitter %.4f)", e.jitter());
    CHECK(e.heartConfidence() < 0.3f, "noise-only heart confidence stays low (%.2f)", e.heartConfidence());
    CHECK(e.breathingConfidence() < 0.3f, "noise-only breathing confidence stays low (%.2f)", e.breathingConfidence());
    CHECK(!e.presence(), "no presence");
}

// ------------------------------------------------------------------ .csirec replay
struct Rec { int kind; int64_t tUs; int seg; std::vector<uint8_t> payload; };
static bool readRec(const char* path, std::vector<Rec>& out) {
    FILE* f = std::fopen(path, "rb"); if (!f) return false;
    char magic[8]; if (std::fread(magic, 1, 8, f) != 8 || std::memcmp(magic, "CSIREC01", 8)) { std::fclose(f); return false; }
    uint8_t hdr[12];
    while (std::fread(hdr, 1, 12, f) == 12) {
        Rec r; r.kind = hdr[0];
        int64_t t = 0; for (int i = 7; i >= 0; --i) t = (t << 8) | hdr[1 + i]; r.tUs = t;
        r.seg = hdr[9]; uint16_t n = (uint16_t)(hdr[10] | (hdr[11] << 8));
        r.payload.resize(n);
        if (std::fread(r.payload.data(), 1, n, f) != n) break;
        out.push_back(std::move(r));
    }
    std::fclose(f); return true;
}

struct SegStat { int n = 0, on = 0; float brSum = 0; int brN = 0; float t0 = -1, tOn = -1; float jSum = 0, wSum = 0; };
static const char* SEG[4] = {"live", "empty", "still", "walk"};

static bool replay(const char* path, bool doAssert, const char* dumpPath) {
    std::vector<Rec> recs;
    if (!readRec(path, recs)) { std::printf("cannot read %s\n", path); return false; }
    RuViewEdge e; e.reset();
    FILE* dump = dumpPath ? std::fopen(dumpPath, "w") : nullptr;
    SegStat st[4]; int lastSeg = 0; int64_t t0 = recs.empty() ? 0 : recs[0].tUs; int frames = 0;
    for (const Rec& r : recs) {
        if (r.kind != 1 || r.payload.size() < 20) continue;
        uint32_t nowMs = (uint32_t)((r.tUs - t0) / 1000) + 1000;
        float tS = (float)(r.tUs - t0) / 1e6f;
        if (r.seg != lastSeg) {
            if (r.seg == 1) e.forceCalibrate(nowMs, RuViewEdge::LEAVE_MS, true);
            else if (lastSeg == 1) e.endCalibration();
            lastSeg = r.seg;
        }
        e.push((const int8_t*)r.payload.data() + 20, (uint16_t)(r.payload.size() - 20), nowMs);
        frames++;
        SegStat& s = st[r.seg & 3];
        if (s.t0 < 0) s.t0 = tS;
        s.n++; s.on += e.presence(); s.jSum += e.jitter(); s.wSum += e.wander();
        if (e.presence() && s.tOn < 0) s.tOn = tS - s.t0;
        if (e.breathingConfidence() >= RuViewEdge::CONF_MIN && e.breathingBpm() > 0) { s.brSum += e.breathingBpm(); s.brN++; }
        if (dump) std::fprintf(dump, "%.3f\t%d\t%.6f\t%.6f\t%d\t%.2f\t%.2f\t%.1f\t%.2f\n", tS, r.seg, e.jitter(), e.wander(), e.presence() ? 1 : 0,
                               e.thresholdJitter(), e.thresholdWander(), e.breathingBpm(), e.breathingConfidence());
    }
    if (dump) std::fclose(dump);
    std::printf("%s: %d frames, layout %u, bins %u, drops %lu, thr_j %.4f thr_w %.4f, calibrated %d\n", path, frames,
                e.layout(), e.binCount(), (unsigned long)e.layoutDrops(), e.thresholdJitter(), e.thresholdWander(), (int)e.calibrated());
    bool ok = true;
    for (int s = 0; s < 4; ++s) {
        if (!st[s].n) continue;
        float pres = 100.0f * st[s].on / st[s].n;
        std::printf("  %-5s n=%5d presence %5.1f%%  jitter %.4f wander %.4f  BR %s  first on %s\n", SEG[s], st[s].n, pres,
                    st[s].jSum / st[s].n, st[s].wSum / st[s].n,
                    st[s].brN ? (std::to_string(st[s].brSum / st[s].brN).substr(0, 4) + " bpm (" + std::to_string(st[s].brN) + " confident frames)").c_str() : "--",
                    st[s].tOn >= 0 ? (std::to_string(st[s].tOn).substr(0, 4) + " s").c_str() : "never");
        if (doAssert) {
            // only the part of "empty" after the minimum calibration is judged (the person is leaving at first)
            if (s == 2 && pres < 90.0f) { ok = false; std::printf("  FAIL still < 90%%\n"); }
            if (s == 3 && pres < 95.0f) { ok = false; std::printf("  FAIL walk < 95%%\n"); }
        }
    }
    return ok;
}

int main(int argc, char** argv) {
    bool doAssert = false; const char* dumpOut = nullptr;
    std::vector<const char*> files;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--assert")) doAssert = true;
        else if (!std::strcmp(argv[i], "--dump") && i + 2 < argc) { files.push_back(argv[i + 1]); dumpOut = argv[i + 2]; i += 2; }
        else files.push_back(argv[i]);
    }
    if (files.empty()) {
        testRandomPhase();
        testSynthetic();
        std::printf(g_fail ? "FAILED (%d)\n" : "all synthetic checks passed\n", g_fail);
        return g_fail ? 1 : 0;
    }
    bool ok = true;
    for (const char* f : files) ok &= replay(f, doAssert, dumpOut);
    return ok ? 0 : 1;
}
