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
    float breathDepth[64] = {0};   // per-bin modulation depth (signed): a chest reflection touches the whole band
    float gain = 1.0f;         // AGC step
    bool walking = false;      // channel changes every frame (random walk of delta)
    bool altAntenna = false;   // router alternates between two channels almost every frame (256-byte mode seen live)
    float base2[64];           // the second antenna's channel
    float fidgetEvery = 0.0f;  // seated person: a 0.5 s movement burst every N seconds (0 = none)
    float noise = 1.0f;        // I/Q noise sigma (LSB)
    Room() {
        std::uniform_real_distribution<float> u(20.0f, 60.0f), ph(0.0f, 6.283f);
        std::uniform_real_distribution<float> depth(0.02f, 0.05f);
        for (int k = 0; k < 64; ++k) { base[k] = u(rng); phase[k] = ph(rng); breathDepth[k] = (k % 2) ? depth(rng) * ((k / 2) % 2 ? 1.0f : -1.0f) : 0.0f; base2[k] = u(rng); }
    }
    void personStill(float strength) {   // channel changed by a body; breathing modulates some bins; fidgets now and then
        std::uniform_real_distribution<float> u(-strength, strength);
        for (int k = 0; k < 64; ++k) delta[k] = u(rng);
        breathAmp = 0.05f; walking = false; fidgetEvery = 45.0f;
    }
    void personWalk() { std::uniform_real_distribution<float> u(-0.3f, 0.3f); for (int k = 0; k < 64; ++k) delta[k] = u(rng); breathAmp = 0; walking = true; fidgetEvery = 0; }
    void empty() { for (int k = 0; k < 64; ++k) delta[k] = 0; breathAmp = 0; walking = false; fidgetEvery = 0; }
    // 384-byte STBC HT20 frame: LLTF (zeros), HT-LTF (signal), STBC-HT-LTF (zeros)
    void frame(float tS, int8_t* out) {
        std::normal_distribution<float> n(0.0f, noise);
        std::uniform_real_distribution<float> ph(0.0f, 6.283f);
        std::memset(out, 0, 384);
        bool fidget = fidgetEvery > 0 && std::fmod(tS, fidgetEvery) < 0.5f;
        if (walking || fidget) {            // the body moves: the channel changes from frame to frame
            std::normal_distribution<float> step(0.0f, 0.08f);
            for (int k = 0; k < 64; ++k) delta[k] += -0.1f * delta[k] + step(rng);   // mean-reverting random walk: keeps changing, never saturates
        }
        float common = ph(rng);   // random per-packet phase offset (CFO), as on real hardware
        bool second = altAntenna && (rng() & 1);
        for (int k = 0; k < 64; ++k) {
            float a = (second ? base2[k] : base[k]) * (1.0f + delta[k]) * gain;
            if (breathAmp > 0 && breathDepth[k] != 0.0f)          // about half the bins see the chest, with varying depth and sign
                a *= 1.0f + breathDepth[k] * std::sin(6.283f * breathHz * tS);
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
    std::printf("synthetic scenario (adaptive baseline ratios)\n");
    RuViewEdge e; e.reset();
    Room room; float t = 0; uint32_t now = 1000;
    // (a) empty room: warm-up + template bootstrap, then 60 s empty
    run(e, room, t, 20.0f, now);
    CHECK(!e.calibrating(), "warm-up over after 20 s (%s)", e.phaseName());
    float fEmpty = run(e, room, t, 60.0f, now);
    std::printf("  baselines: jitter %.4f wander %.4f; empty presence %.1f%%\n", e.baselineJitter(), e.baselineWander(), 100 * fEmpty);
    CHECK(e.baselineJitter() > 0 && e.baselineWander() > 0, "baselines learned");
    CHECK(fEmpty < 0.01f, "empty room stays absent (%.1f%%)", 100 * fEmpty);
    // (b) person walks in (motion event) and sits still with breathing: presence must hold through stillness
    room.personWalk();
    run(e, room, t, 3.0f, now);
    CHECK(e.presence() && e.moving(), "walking in detected as IN moving");
    room.personStill(0.3f);
    float brBpm = 0, brC = 0;
    float fStill = run(e, room, t, 150.0f, now, &brBpm, &brC);
    std::printf("  still:  presence %.1f%%  wander x%.1f  moving %d  BR %.1f bpm conf %.2f  HR %.1f conf %.2f  blocks %lu\n",
                100 * fStill, e.ratioWander(), (int)e.moving(), brBpm, brC, e.heartRateBpm(), e.heartConfidence(), (unsigned long)e.blocks());
    CHECK(fStill > 0.95f, "still person kept present beyond the 60 s hold through wander (%.1f%%)", 100 * fStill);
    CHECK(!e.moving(), "not moving while still");
    // breathing needs fidget-free blocks: a 100 s motionless stretch (presence holds through wander + memory)
    room.fidgetEvery = 0;
    run(e, room, t, 100.0f, now, &brBpm, &brC);
    std::printf("  motionless: BR %.1f bpm conf %.2f\n", brBpm, brC);
    CHECK(std::fabs(brBpm - 15.0f) <= 1.5f, "breathing 15 +- 1.5 bpm (got %.1f)", brBpm);
    room.fidgetEvery = 45.0f;
    CHECK(!e.fall(), "fall disabled");
    // (c) walking
    room.personWalk();
    float fWalk = 0; { int frames = 0, on = 0, mov = 0; float tEnd = t + 20; int8_t buf[384];
        while (t < tEnd) { room.frame(t, buf); e.push(buf, 384, now); frames++; on += e.presence(); mov += e.moving(); t += 0.05f; now += 50; }
        fWalk = (float)on / frames; std::printf("  walk:   presence %.1f%%  moving %.1f%%\n", 100 * fWalk, 100.0f * mov / frames);
        CHECK(mov > frames / 2, "walking reads as moving"); }
    CHECK(fWalk > 0.95f, "walking detected (%.1f%%)", 100 * fWalk);
    // (d) leaves: presence must clear within the 60 s hold (+ a little)
    room.empty();
    float tLeft = t; bool off = false; { int8_t buf[384]; while (t < tLeft + 90) { room.frame(t, buf); e.push(buf, 384, now); if (!e.presence() && !off) { off = true; std::printf("  leave:  off after %.1f s\n", t - tLeft); } t += 0.05f; now += 50; } }
    CHECK(off && !e.presence(), "presence clears after the person leaves");
    float fAfter = run(e, room, t, 30.0f, now);
    CHECK(fAfter < 0.02f, "stays absent after leaving (%.1f%%)", 100 * fAfter);
    // (e) AGC gain step in an empty room must not trigger presence
    room.gain = 1.5f;
    float fAgc = run(e, room, t, 20.0f, now);
    std::printf("  agc x1.5 empty: presence %.1f%%\n", 100 * fAgc);
    CHECK(fAgc < 0.02f, "gain step does not fake presence (%.1f%%)", 100 * fAgc);
    room.gain = 1.0f;
    // (f) a new router regime (5x noisier, permanent) is absorbed: after the slow adaptation the room is absent again
    room.noise = 5.0f;
    float fFirst = run(e, room, t, 900.0f, now);
    float fRegime = run(e, room, t, 300.0f, now);
    std::printf("  noisier regime: presence in the first 15 min %.1f%%, in the 5 min after %.1f%%  baseline jitter %.4f\n", 100 * fFirst, 100 * fRegime, e.baselineJitter());
    CHECK(fRegime < 0.02f, "a new empty-room regime is absorbed by the slow baseline within 15 min (%.1f%% after)", 100 * fRegime);
    room.noise = 1.0f;
    // (g) 128-byte LLTF-only frames are dropped (counted); 256-byte frames are a second layout
    int8_t small[128]; for (int i = 0; i < 128; ++i) small[i] = (int8_t)(10 + (i % 7)); uint32_t drops = e.layoutDrops();
    e.push(small, 128, now + 50);
    CHECK(e.layoutDrops() == drops + 1, "LLTF-only frame dropped and counted");
    { int8_t full[384]; room.frame(t, full); uint32_t fr = e.frames(); e.push(full, 256, now + 100); CHECK(e.frames() == fr + 1, "256-byte frames processed as their own layout"); }
    // (i) router antenna alternation (the live 256-byte mode): empty room must read OUT, a person must still be seen
    room.noise = 1.0f; room.empty(); room.altAntenna = true;
    run(e, room, t, 30.0f, now);                     // states get discovered and bootstrapped
    float fAlt = run(e, room, t, 120.0f, now);
    std::printf("  alternating antennas, empty: presence %.1f%%  jitter %.4f base %.4f  states %u\n", 100 * fAlt, e.jitter(), e.baselineJitter(), (unsigned)e.states(1));
    CHECK(e.states(1) >= 2, "two router states discovered (%u)", (unsigned)e.states(1));
    CHECK(fAlt < 0.05f, "alternating router does not fake presence (%.1f%%)", 100 * fAlt);
    room.personWalk(); run(e, room, t, 3.0f, now); room.personStill(0.3f);
    float fAltStill = run(e, room, t, 90.0f, now);
    std::printf("  alternating antennas, still person: presence %.1f%%  wander x%.1f\n", 100 * fAltStill, e.ratioWander());
    CHECK(fAltStill > 0.9f, "person detected despite alternation (%.1f%%)", 100 * fAltStill);
    room.empty(); run(e, room, t, 90.0f, now); CHECK(!e.presence(), "clears after leaving under alternation");
    room.altAntenna = false;
    // (h) restart warms up again
    e.restart();
    CHECK(e.calibrating() && !e.presence(), "restart clears state");
}

static void testRandomPhase() {
    std::printf("random phase, constant amplitude (the old failure mode)\n");
    RuViewEdge e; e.reset();
    Room room; room.noise = 0.5f; float t = 0; uint32_t now = 500;
    run(e, room, t, 70.0f, now);
    std::printf("  jitter %.5f (base %.5f)  wander %.5f (base %.5f)  noise-only HR %.0f conf %.2f  BR %.0f conf %.2f\n", e.jitter(), e.baselineJitter(),
                e.wander(), e.baselineWander(), e.heartRateBpm(), e.heartConfidence(), e.breathingBpm(), e.breathingConfidence());
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

struct SegStat { int n = 0, on = 0, nT = 0, onT = 0; float brSum = 0; int brN = 0; float t0 = -1, t1 = -1, tOn = -1; float jSum = 0, wSum = 0; int nG = 0, onG = 0; };
static const char* SEG[4] = {"live", "empty", "still", "walk"};

static bool replay(const char* path, bool doAssert, const char* dumpPath) {
    std::vector<Rec> recs;
    if (!readRec(path, recs)) { std::printf("cannot read %s\n", path); return false; }
    RuViewEdge e; e.reset();
    FILE* dump = dumpPath ? std::fopen(dumpPath, "w") : nullptr;
    SegStat st[4]; int lastSeg = 0; (void)lastSeg; int64_t t0 = recs.empty() ? 0 : recs[0].tUs; int frames = 0;
    for (const Rec& r : recs) {
        if (r.kind != 1 || r.payload.size() < 20) continue;
        uint32_t nowMs = (uint32_t)((r.tUs - t0) / 1000) + 1000;
        float tS = (float)(r.tUs - t0) / 1e6f;
        lastSeg = r.seg;
        e.push((const int8_t*)r.payload.data() + 20, (uint16_t)(r.payload.size() - 20), nowMs);
        frames++;
        SegStat& s = st[r.seg & 3];
        if (s.t0 < 0) s.t0 = tS;
        s.t1 = tS;
        s.n++; s.on += e.presence(); s.jSum += e.jitter(); s.wSum += e.wander();
        bool templated = e.templates() & (r.payload.size() - 20 >= 384 ? 2 : 1);   // this frame's layout has a template
        if (templated) { s.nT++; s.onT += e.presence(); }
        (void)0;
        if (e.presence() && s.tOn < 0) s.tOn = tS - s.t0;
        if (e.breathingBpm() > 0) { s.brSum += e.breathingBpm(); s.brN++; }
        if (dump) std::fprintf(dump, "%.3f\t%d\t%.6f\t%.6f\t%d\t%.2f\t%.2f\t%.1f\t%.2f\n", tS, r.seg, e.jitter(), e.wander(), e.presence() ? 1 : 0,
                               e.thresholdJitter(), e.thresholdWander(), e.breathingBpm(), e.breathingConfidence());
    }
    if (dump) std::fclose(dump);
    // second pass for the empty gate: the person walks back in before pressing, so judge the empty
    // segment only up to 15 s before its end (over templated frames)
    { RuViewEdge e2; e2.reset(); int last2 = 0; (void)last2;
      for (const Rec& r : recs) {
        if (r.kind != 1 || r.payload.size() < 20) continue;
        uint32_t nowMs = (uint32_t)((r.tUs - t0) / 1000) + 1000; float tS = (float)(r.tUs - t0) / 1e6f;
        last2 = r.seg;
        e2.push((const int8_t*)r.payload.data() + 20, (uint16_t)(r.payload.size() - 20), nowMs);
        if (r.seg == 1 && tS >= st[1].t0 + 30.0f && tS <= st[1].t1 - 15.0f && (e2.templates() & (r.payload.size() - 20 >= 384 ? 2 : 1))) { st[1].nG++; st[1].onG += e2.presence(); }
      } }
    std::printf("%s: %d frames, vitals layout %u, templates %u, drops %lu, untemplated %lu, blocks %lu, thr_j %.4f thr_w %.4f, live %d\n", path, frames,
                e.layout(), e.templates(), (unsigned long)e.layoutDrops(), (unsigned long)e.untemplated(), (unsigned long)e.blocks(),
                e.thresholdJitter(), e.thresholdWander(), (int)!e.calibrating());
    bool ok = true;
    for (int s = 0; s < 4; ++s) {
        if (!st[s].n) continue;
        float pres = 100.0f * st[s].on / st[s].n;
        float presT = st[s].nT ? 100.0f * st[s].onT / st[s].nT : -1.0f;
        std::printf("  %-5s n=%5d presence %5.1f%% (templated frames %d: %5.1f%%)  jitter %.4f wander %.4f  BR %s  first on %s\n", SEG[s], st[s].n, pres,
                    st[s].nT, presT, st[s].jSum / st[s].n, st[s].wSum / st[s].n,
                    st[s].brN ? (std::to_string(st[s].brSum / st[s].brN).substr(0, 4) + " bpm (" + std::to_string(st[s].brN) + " frames with a reading)").c_str() : "--",
                    st[s].tOn >= 0 ? (std::to_string(st[s].tOn).substr(0, 4) + " s").c_str() : "never");
        if (doAssert) {
            // only the part of "empty" after the minimum calibration is judged (the person is leaving at first)
            // gates apply to frames whose layout had a template; untemplated layouts are a known limitation
            if (s == 1 && st[s].nG && 100.0f * st[s].onG / st[s].nG > 1.0f) { ok = false; std::printf("  FAIL empty > 1%% (%.1f%% from 30 s in to 15 s before the press)\n", 100.0f * st[s].onG / st[s].nG); }
            else if (s == 1 && st[s].nG) std::printf("  empty gate: %.1f%% over %d templated frames (30 s in .. 15 s before the press)\n", 100.0f * st[s].onG / st[s].nG, st[s].nG);
            if (s == 2 && st[s].nT && presT < 90.0f) { ok = false; std::printf("  FAIL still < 90%%\n"); }
            if (s == 3 && st[s].nT && presT < 95.0f) { ok = false; std::printf("  FAIL walk < 95%%\n"); }
            if (s == 3 && !st[s].nT) std::printf("  note: walk frames were all of an untemplated layout; presence there relied on jitter only\n");
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
