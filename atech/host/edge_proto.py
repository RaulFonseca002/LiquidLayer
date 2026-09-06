#!/usr/bin/env python3
"""edge_proto.py — offline reference of the amplitude-correlation sensing engine.

Replays a .csirec recording made with `csi_sink.py --record` during the button protocol
(segments: 1 empty room, 2 sitting still, 3 walking) and reports whether the planned
firmware algorithm separates the segments, with which constants, and whether breathing
is recoverable from the STILL segment. Stdlib only, deliberately mirrors RuViewEdge
(modules/ruview_csi/ruview_edge.cpp) so the two can be cross-checked (--dump-features).

Algorithm (Espressif esp-radar style, single antenna):
  a_t[k]   = |CSI_t[k]| over the data bins of one LTF block, L2-normalised per frame
  jitter_t = 1 - corr(a_t, a_{t-1})           motion, needs no calibration
  wander_t = 1 - corr(a_t, a_ref)             presence vs. the empty-room template a_ref
  both EMA-smoothed (alpha 0.2); on threshold = mean + 4*sigma, off = mean + 2*sigma of the
  SMOOTHED values over the empty-room calibration; Schmitt trigger with N-frame confirmation and
  a hold time. The breathing report here is informational (plain autocorrelation peak); the
  firmware additionally subtracts the band-pass filter's own noise autocorrelation from the
  confidence, so use the native replay (tests/edge/run.sh REC.csirec) for the authoritative numbers.

    python3 edge_proto.py REC.csirec [--leave-s 10] [--k-sigma 4] [--alpha 0.2]
    python3 edge_proto.py REC.csirec --dump-features OUT.tsv     # t seg jitter wander (raw) per frame
"""
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import csirec  # noqa: E402
from ruview_packets import SEGMENT_NAMES  # noqa: E402

PILOTS = {7, 21, 43, 57}
HT_BINS = [i for i in list(range(1, 29)) + list(range(36, 64)) if i not in PILOTS]      # 52 bins
LLTF_BINS = [i for i in list(range(1, 27)) + list(range(38, 64)) if i not in PILOTS]    # 48 bins


def layout_of(iq_len: int) -> int:
    return iq_len // 128          # 1 LLTF, 2 +HT-LTF, 3 +STBC-HT-LTF


def amplitude_vector(iq: bytes) -> list[float] | None:
    """Amplitudes of the data bins of the preferred LTF block, L2-normalised. None if layout unknown."""
    lay = layout_of(len(iq))
    if lay >= 2:
        base, bins = 128, HT_BINS          # HT-LTF block = subcarriers 64..127 = bytes 128..255
    elif lay == 1:
        base, bins = 0, LLTF_BINS
    else:
        return None
    v = []
    for b in bins:
        o = base + 2 * b
        im = iq[o] - 256 if iq[o] > 127 else iq[o]
        re = iq[o + 1] - 256 if iq[o + 1] > 127 else iq[o + 1]
        v.append(math.hypot(im, re))
    n = math.sqrt(sum(x * x for x in v))
    if n < 1e-6:
        return None
    return [x / n for x in v]


def corr(a: list[float], b: list[float]) -> float:
    n = len(a)
    ma = sum(a) / n
    mb = sum(b) / n
    sab = sa = sb = 0.0
    for x, y in zip(a, b):
        dx, dy = x - ma, y - mb
        sab += dx * dy
        sa += dx * dx
        sb += dy * dy
    d = math.sqrt(sa * sb)
    return sab / d if d > 1e-12 else 0.0


def stats(xs: list[float]) -> tuple[float, float, float, float]:
    if not xs:
        return (0.0, 0.0, 0.0, 0.0)
    n = len(xs)
    m = sum(xs) / n
    var = sum((x - m) ** 2 for x in xs) / max(n - 1, 1)
    s = sorted(xs)
    return (m, math.sqrt(var), s[int(0.95 * (n - 1))], s[-1])


class Engine:
    """Python twin of RuViewEdge's presence path (constants are the planned firmware defaults)."""

    def __init__(self, alpha=0.2, k_sigma=4.0, k_off=2.0, on_frames=3, off_frames=20, hold_s=3.0,
                 floor_j=0.002, floor_w=0.005, default_thr_j=0.05):
        self.alpha, self.k, self.k_off, self.on_n, self.off_n, self.hold_s = alpha, k_sigma, k_off, on_frames, off_frames, hold_s
        self.floor_j, self.floor_w, self.default_thr_j = floor_j, floor_w, default_thr_j
        self.reset()

    def reset(self):
        self.prev = None
        self.ref = None
        self.sj = self.sw = 0.0
        self.have_s = False
        self.thr_j = self.default_thr_j
        self.off_j = 0.5 * self.default_thr_j
        self.thr_w = None
        self.off_w = None
        self.presence = False
        self.above = self.below = 0
        self.on_since = None
        # calibration accumulators
        self.cal_vecs = 0
        self.cal_sum = None
        self.cal_j = []
        self.cal_w = []

    # ---- calibration in two passes over the empty-room frames ----
    def calib_template(self, vec):
        if self.cal_sum is None:
            self.cal_sum = [0.0] * len(vec)
        if len(vec) != len(self.cal_sum):
            return
        for i, x in enumerate(vec):
            self.cal_sum[i] += x
        self.cal_vecs += 1

    def finish_template(self):
        if not self.cal_vecs:
            return False
        ref = [x / self.cal_vecs for x in self.cal_sum]
        n = math.sqrt(sum(x * x for x in ref))
        self.ref = [x / n for x in ref]
        return True

    def features(self, vec):
        """Raw jitter/wander for one frame and update the smoothed values. Returns (j, w) or None."""
        if self.ref is not None and len(vec) != len(self.ref):
            return None
        if self.prev is None or len(self.prev) != len(vec):
            self.prev = vec
            return None
        j = max(0.0, 1.0 - corr(vec, self.prev))
        w = max(0.0, 1.0 - corr(vec, self.ref)) if self.ref is not None else 0.0
        self.prev = vec
        if not self.have_s:
            self.sj, self.sw, self.have_s = j, w, True
        else:
            self.sj += self.alpha * (j - self.sj)
            self.sw += self.alpha * (w - self.sw)
        return j, w

    def calib_stats(self):
        self.cal_j.append(self.sj)
        self.cal_w.append(self.sw)

    def finish_thresholds(self):
        mj, sdj, _, _ = stats(self.cal_j)
        mw, sdw, _, _ = stats(self.cal_w)
        self.thr_j = max(self.floor_j, min(1.5, mj + self.k * sdj))
        self.thr_w = max(self.floor_w, min(1.5, mw + self.k * sdw))
        # off levels = mean + k_off*sigma, always below the on level (RuViewEdge::finishCalibration)
        self.off_j = mj + self.k_off * sdj
        if self.off_j >= self.thr_j:
            self.off_j = 0.5 * (mj + self.thr_j)
        self.off_w = mw + self.k_off * sdw
        if self.off_w >= self.thr_w:
            self.off_w = 0.5 * (mw + self.thr_w)
        return (mj, sdj, mw, sdw)

    # ---- detection ----
    def step(self, t_s):
        hi = self.sj > self.thr_j or (self.thr_w is not None and self.sw > self.thr_w)
        lo = self.sj < self.off_j and (self.off_w is None or self.sw < self.off_w)
        if not self.presence:
            self.above = self.above + 1 if hi else 0
            if self.above >= self.on_n:
                self.presence, self.on_since, self.below = True, t_s, 0
        else:
            self.below = self.below + 1 if lo else 0
            if self.below >= self.off_n and t_s - self.on_since >= self.hold_s:
                self.presence, self.above = False, 0
        return self.presence


# ---------------------------------------------------------------- breathing (STILL segment)

def resample(ts, xs, fs):
    """Linear interpolation of an irregular series onto a uniform grid at fs Hz."""
    if len(ts) < 2:
        return []
    out = []
    t = ts[0]
    i = 0
    while t <= ts[-1]:
        while i + 1 < len(ts) and ts[i + 1] < t:
            i += 1
        if i + 1 >= len(ts):
            break
        t0, t1 = ts[i], ts[i + 1]
        f = (t - t0) / (t1 - t0) if t1 > t0 else 0.0
        out.append(xs[i] + f * (xs[i + 1] - xs[i]))
        t += 1.0 / fs
    return out


def moving_average_detrend(x, win):
    out = []
    acc = 0.0
    q = []
    for v in x:
        q.append(v)
        acc += v
        if len(q) > win:
            acc -= q.pop(0)
        out.append(v - acc / len(q))
    return out


def bandpass(x, fs, flo, fhi):
    """RBJ constant-Q bandpass (the corrected design: Q = f0 / bandwidth), applied twice for a steeper skirt."""
    f0 = math.sqrt(flo * fhi)
    q = f0 / (fhi - flo)
    w0 = 2 * math.pi * f0 / fs
    alpha = math.sin(w0) / (2 * q)
    b0, b1, b2 = alpha, 0.0, -alpha
    a0, a1, a2 = 1 + alpha, -2 * math.cos(w0), 1 - alpha
    b0, b1, b2, a1, a2 = b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0

    def run(sig):
        x1 = x2 = y1 = y2 = 0.0
        out = []
        for v in sig:
            y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
            x2, x1, y2, y1 = x1, v, y1, y
            out.append(y)
        return out
    return run(run(x))


def autocorr_bpm(sig, fs, bpm_lo, bpm_hi):
    """Normalised autocorrelation peak in the lag range -> (bpm, confidence=peak/r0)."""
    n = len(sig)
    if n < 4:
        return 0.0, 0.0
    m = sum(sig) / n
    s = [v - m for v in sig]
    r0 = sum(v * v for v in s)
    if r0 < 1e-12:
        return 0.0, 0.0
    lag_lo = int(fs * 60.0 / bpm_hi)
    lag_hi = min(int(fs * 60.0 / bpm_lo), n // 2)
    best, best_lag = -1.0, 0
    for lag in range(lag_lo, lag_hi + 1):
        r = sum(s[i] * s[i + lag] for i in range(n - lag)) / r0
        if r > best:
            best, best_lag = r, lag
    if best_lag <= 0:
        return 0.0, 0.0
    return 60.0 * fs / best_lag, max(0.0, best)


def breathing_report(frames, fs=20.0, window_s=30.0, step_s=10.0, top_k=5):
    """frames: list of (t_s, vec) in the STILL segment. Prints per-window breathing estimates."""
    if len(frames) < fs * window_s:
        return [f"  breathing: only {len(frames)} frames in STILL (< {window_s:.0f} s), skipped"]
    nb = len(frames[0][1])
    ts = [f[0] for f in frames]
    series = []
    for k in range(nb):
        xs = [f[1][k] for f in frames]
        u = resample(ts, xs, fs)
        series.append(u)
    n = min(len(s) for s in series)
    lines = []
    w = int(window_s * fs)
    st = int(step_s * fs)
    estimates = []
    for start in range(0, n - w + 1, st):
        cands = []
        for k in range(nb):
            seg = series[k][start:start + w]
            d = moving_average_detrend(seg, int(4 * fs))
            bp = bandpass(d, fs, 0.1, 0.5)
            tot = sum(v * v for v in d) or 1e-12
            inb = sum(v * v for v in bp[int(2 * fs):])   # skip filter transient
            cands.append((inb / tot, bp))
        cands.sort(key=lambda c: -c[0])
        chosen = cands[:top_k]
        # fuse: normalise each in-band signal to unit energy, then sum autocorrelations via the summed signal
        fused = [0.0] * w
        for ratio, bp in chosen:
            e = math.sqrt(sum(v * v for v in bp)) or 1e-12
            for i, v in enumerate(bp):
                fused[i] += v / e
        bpm, conf = autocorr_bpm(fused[int(2 * fs):], fs, 6, 30)
        estimates.append((start / fs, bpm, conf, chosen[0][0]))
        lines.append(f"  t={start / fs:5.0f}s  breathing {bpm:5.1f} bpm  confidence {conf:.2f}  best in-band ratio {chosen[0][0]:.2f}")
    good = [e for e in estimates if e[2] >= 0.3]
    if good:
        bpms = sorted(e[1] for e in good)
        lines.append(f"  -> {len(good)}/{len(estimates)} windows confident (>=0.30); median {bpms[len(bpms)//2]:.1f} bpm, "
                     f"range {bpms[0]:.1f}-{bpms[-1]:.1f}")
    else:
        lines.append("  -> no confident breathing estimate (all windows < 0.30)")
    return lines


# ---------------------------------------------------------------- main

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("rec")
    ap.add_argument("--leave-s", type=float, default=10.0, help="seconds of EMPTY to skip (person leaving)")
    ap.add_argument("--k-sigma", type=float, default=4.0)
    ap.add_argument("--alpha", type=float, default=0.2)
    ap.add_argument("--dump-features", default="", help="write t seg jitter wander (raw, unsmoothed) per frame")
    ap.add_argument("--no-breathing", action="store_true")
    args = ap.parse_args()

    frames = []          # (t_s, seg, vec)
    layouts = {}
    for t_s, seg, rssi, iq in csirec.csi_frames(args.rec):
        layouts[layout_of(len(iq))] = layouts.get(layout_of(len(iq)), 0) + 1
        vec = amplitude_vector(iq)
        if vec is not None:
            frames.append((t_s, seg, vec))
    print(csirec.summary(args.rec))
    print(f"layouts (bytes/128 -> frames): {layouts}")
    if not frames:
        print("no usable CSI frames")
        return

    eng = Engine(alpha=args.alpha, k_sigma=args.k_sigma)
    empty = [f for f in frames if f[1] == 1]
    if not empty:
        print("no EMPTY segment (1) in the recording: cannot calibrate; reporting jitter only")
    else:
        t_empty0 = empty[0][0]
        cal = [f for f in empty if f[0] - t_empty0 >= args.leave_s]
        for _, _, vec in cal:
            eng.calib_template(vec)
        eng.finish_template()
        # second pass: smoothed statistics over the same frames (prev/EMA reset first)
        eng.prev, eng.have_s = None, False
        for _, _, vec in cal:
            if eng.features(vec) is not None:
                eng.calib_stats()
        mj, sdj, mw, sdw = eng.finish_thresholds()
        print(f"calibration: {len(cal)} frames ({cal[-1][0] - cal[0][0]:.0f} s) after skipping {args.leave_s:.0f} s")
        print(f"  smoothed jitter  mean {mj:.4f} sigma {sdj:.4f} -> on {eng.thr_j:.4f} off {eng.off_j:.4f}")
        print(f"  smoothed wander  mean {mw:.4f} sigma {sdw:.4f} -> on {eng.thr_w:.4f} off {eng.off_w:.4f}")

    # full replay
    eng.prev, eng.have_s, eng.presence = None, False, False
    per = {}
    dump = open(args.dump_features, "w") if args.dump_features else None
    for t_s, seg, vec in frames:
        f = eng.features(vec)
        if f is None:
            continue
        pres = eng.step(t_s)
        d = per.setdefault(seg, {"j": [], "w": [], "p": 0, "n": 0, "t_on": None, "t0": t_s})
        d["j"].append(eng.sj)
        d["w"].append(eng.sw)
        d["n"] += 1
        d["p"] += int(pres)
        if pres and d["t_on"] is None:
            d["t_on"] = t_s - d["t0"]
        if dump:
            dump.write(f"{t_s:.3f}\t{seg}\t{f[0]:.6f}\t{f[1]:.6f}\n")
    if dump:
        dump.close()

    print("per segment (smoothed features; presence fraction with the calibrated thresholds):")
    base_j = stats(per[1]["j"]) if 1 in per else None
    base_w = stats(per[1]["w"]) if 1 in per else None
    for seg in sorted(per):
        d = per[seg]
        mj, sj, pj, xj = stats(d["j"])
        mw, sw, pw, xw = stats(d["w"])
        sep = ""
        if base_j and seg != 1:
            sep = (f"  separation j {(mj - base_j[0]) / max(base_j[1], 1e-9):5.1f}σ"
                   f"  w {(mw - base_w[0]) / max(base_w[1], 1e-9):5.1f}σ")
        on = f"  first on at +{d['t_on']:.1f}s" if d["t_on"] is not None else ""
        print(f"  {seg} {SEGMENT_NAMES[seg]:5s} n={d['n']:5d}  jitter {mj:.4f}±{sj:.4f} p95 {pj:.4f}  "
              f"wander {mw:.4f}±{sw:.4f} p95 {pw:.4f}  presence {100.0 * d['p'] / d['n']:5.1f}%{sep}{on}")

    if not args.no_breathing and 2 in per:
        print("breathing (STILL segment, 30 s windows, top-5 bins by in-band power, autocorrelation):")
        still = [(t, vec) for t, seg, vec in frames if seg == 2]
        for line in breathing_report(still):
            print(line)


if __name__ == "__main__":
    main()
