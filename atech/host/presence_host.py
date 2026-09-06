#!/usr/bin/env python3
"""presence_host.py — live in/out-of-room decision on the laptop from the board's raw CSI stream.

Runs candidate F from analysis/candidates.py (adaptive baseline ratio + hold) on the UDP stream
the board already sends (ADR-018 CSI frames + NodeStatus), prints one line per state change and a
status line every 5 s, and appends a per-second CSV. The board is a dumb sensor here; iterating on
the algorithm needs no reflash. Requires numpy (atech venv).

    ~/.atech/venv/bin/python atech/host/presence_host.py [--port 5005] [--csv out.csv] [--liquid]

Decision (see docs/CSI_PRESENCE_RESEARCH.md and analysis/): per frame, HT-LTF amplitude vector,
jitter = 1 - corr with the previous frame of the same layout, wander = 1 - corr with a per-layout
template learned from quiet, absent frames. A tracked baseline of each (fast while absent and
quiet, slow otherwise, so a new router regime is absorbed) turns them into ratios; a motion event
is jitter > R_j x baseline for 3 of 5 frames; presence = event within HOLD_S, or wander > R_w x
baseline while an event happened within MEMORY_S. --liquid also emits ExternalObservation NDJSON
(node/N/vitals.presence) like liquid_bridge.py.
"""
from __future__ import annotations

import argparse
import json
import socket
import struct
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ruview_packets import MAGIC_CSI, MAGIC_STATUS, parse  # noqa: E402
from csirec import Recorder  # noqa: E402

PILOTS = {7, 21, 43, 57}
HT_BINS = np.array([i for i in list(range(1, 29)) + list(range(36, 64)) if i not in PILOTS])

P = {"R_j": 4.0, "R_w": 6.0, "k": 3, "n": 5, "hold_s": 60.0, "memory_s": 120.0,
     "tau_fast_s": 20.0, "tau_slow_s": 900.0, "tau_template_s": 120.0, "floor_j": 0.003, "floor_w": 0.01,
     "template_frames": 300, "state_ready": 100, "states": 3, "far_d": 0.4, "interleave_n": 20, "interleave_min": 5,
     "off_frames": 20,
     "warmup_s": 15.0}


def amp_vector(iq: bytes):
    if len(iq) < 256:
        return None, 0
    b = np.frombuffer(iq, dtype=np.int8)
    im = b[128 + 2 * HT_BINS].astype(np.float32); re = b[129 + 2 * HT_BINS].astype(np.float32)
    a = np.sqrt(im * im + re * re)
    n = np.linalg.norm(a)
    return (a / n if n > 1e-6 else None), (3 if len(iq) >= 384 else 2)


def corr(a, b):
    a0 = a - a.mean(); b0 = b - b.mean()
    d = np.sqrt((a0 * a0).sum() * (b0 * b0).sum())
    return float((a0 * b0).sum() / d) if d > 1e-12 else 0.0


class Detector:
    def __init__(self, p=P):
        self.p = p
        self.prev = {}; self.prev_t = {}          # keyed by (layout, state)
        self.tpl = {}; self.tpl_sum = {}; self.tpl_n = {}; self.last_used = {}   # keyed by (layout, state)
        self.n_states = {}; self.near_hist = {}; self.far_hist = {}                # keyed by layout
        self.bj = np.nan; self.bw = np.nan
        self.hist = []; self.whist = []; self.off_count = 0; self.last_event = -1e9; self.on = False; self.on_since = 0.0
        self.last_t = None
        self.t0 = None   # first frame time: no events during the warm-up, baselines learn from medians
        self.warm_j = []; self.warm_w = []
        self.rj = np.nan; self.rw = np.nan; self.j = np.nan; self.w = np.nan

    def seed_state(self, lay, s, a, t):
        key = (lay, s)
        self.tpl[key] = a.copy(); self.tpl_sum[key] = a.astype(np.float64).copy(); self.tpl_n[key] = 1; self.last_used[key] = t
        self.prev.pop(key, None)
        self.n_states[lay] = max(self.n_states.get(lay, 0), s + 1)

    def assign_state(self, lay, a, t):
        p = self.p
        n = self.n_states.get(lay, 0)
        if n == 0:
            self.seed_state(lay, 0, a, t); self.near_hist[lay] = []; self.far_hist[lay] = []
            return 0, 0.0, True
        ds = [1.0 - corr(a, self.tpl[(lay, s)]) for s in range(n)]
        best = int(np.argmin(ds)); d = ds[best]
        far = d > p["far_d"]
        self.far_hist[lay] = (self.far_hist[lay] + [far])[-p["interleave_n"]:]
        self.near_hist[lay] = (self.near_hist[lay] + [not far])[-p["interleave_n"]:]
        if far and sum(self.far_hist[lay]) >= p["interleave_min"] and sum(self.near_hist[lay]) >= p["interleave_min"]:
            slot = n if n < p["states"] else min(range(n), key=lambda s: self.last_used.get((lay, s), 0))
            self.seed_state(lay, slot, a, t); self.far_hist[lay] = []; self.near_hist[lay] = []
            return slot, 0.0, True
        return best, d, False

    def push(self, a, lay, t):
        p = self.p
        dt = (t - self.last_t) if self.last_t is not None else 0.05
        if dt > 5.0:
            self.bj = self.bw = np.nan
        self.last_t = t
        # router sub-state: nearest template of this layout; seed a new one only when near and far frames interleave
        st, d, seeded = self.assign_state(lay, a, t)
        key = (lay, st); self.last_used[key] = t
        # jitter against the previous frame of the same layout and state (within 1 s)
        j = np.nan
        if key in self.prev and t - self.prev_t[key] <= 1.0:
            j = max(0.0, 1.0 - corr(a, self.prev[key]))
        self.prev[key] = a; self.prev_t[key] = t
        # state template: running mean over its first frames (far frames never enter it)
        if (seeded or d <= p["far_d"]) and self.tpl_n[key] < p["template_frames"]:
            if not seeded:
                self.tpl_sum[key] += a; self.tpl_n[key] += 1
            T = self.tpl_sum[key] / self.tpl_n[key]; self.tpl[key] = T / np.linalg.norm(T)
        w = np.nan
        if self.tpl_n[key] >= p["state_ready"]:
            w = max(0.0, 1.0 - corr(a, self.tpl[key]))
        # warm-up: collect, learn robust baselines, decide nothing
        if self.t0 is None:
            self.t0 = t
        warming = (t - self.t0) < p["warmup_s"]
        if warming:
            if not np.isnan(j): self.warm_j.append(j)
            if not np.isnan(w): self.warm_w.append(w)
            if self.warm_j: self.bj = max(float(np.median(self.warm_j)), p["floor_j"])
            if self.warm_w: self.bw = max(float(np.median(self.warm_w)), p["floor_w"])
            self.j, self.w = j, w
            return self.on
        # ratios
        rj = rw = np.nan
        if not np.isnan(j):
            if np.isnan(self.bj): self.bj = max(j, p["floor_j"])
            rj = j / max(self.bj, p["floor_j"])
        if not np.isnan(w):
            if np.isnan(self.bw): self.bw = max(w, p["floor_w"])
            rw = w / max(self.bw, p["floor_w"])
        ev = (not np.isnan(rj)) and rj > p["R_j"]
        self.hist.append(ev); self.hist = self.hist[-p["n"]:]
        self.whist.append((not np.isnan(rw)) and rw > p["R_w"]); self.whist = self.whist[-p["n"]:]
        if sum(self.hist) >= p["k"]:
            self.last_event = t
        moved = (t - self.last_event) <= p["hold_s"]
        remembered = (t - self.last_event) <= p["memory_s"]
        still_body = sum(self.whist) >= p["k"] and remembered
        if moved or still_body:
            self.off_count = 0
            if not self.on:
                self.on = True; self.on_since = t
        elif self.on:
            self.off_count += 1
            if self.off_count >= p["off_frames"]:
                self.on = False; self.off_count = 0
        # baselines and template tracking
        quiet = (not np.isnan(rj)) and rj < 2.0
        tau = p["tau_fast_s"] if (not self.on and quiet) else p["tau_slow_s"]
        g = min(1.0, dt / tau)
        if not np.isnan(j): self.bj += g * (j - self.bj)
        if not np.isnan(w) and not self.on: self.bw += g * (w - self.bw)   # wander baseline only while absent
        if self.tpl_n[key] >= p["template_frames"] and not self.on and quiet:
            gt = min(1.0, dt / p["tau_template_s"])
            T = self.tpl[key] + gt * (a - self.tpl[key]); self.tpl[key] = T / np.linalg.norm(T)
        self.j, self.w, self.rj, self.rw = j, w, rj, rw
        return self.on


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=5005)
    ap.add_argument("--csv", default="")
    ap.add_argument("--liquid", action="store_true", help="emit ExternalObservation NDJSON on stdout")
    ap.add_argument("--node", type=int, default=1)
    ap.add_argument("--record", default="", help="also write every packet to this .csirec (replaces csi_sink --record; one UDP port)")
    ap.add_argument("--rotate-s", type=float, default=3600.0, help="start a new .csirec every N seconds (suffix _NNN)")
    args = ap.parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", args.port)); sock.settimeout(0.5)
    det = Detector()
    csv = open(args.csv, "a") if args.csv else None
    if csv and csv.tell() == 0:
        csv.write("t,hms,label,layout,rssi,jitter,wander,ratio_j,ratio_w,baseline_j,baseline_w,present\n")
    label = 0; rssi = 0; last_status = 0.0; last_csv = 0.0; last_state = None; rev = 0; last_uptime = None
    rec = None; rec_start = 0.0; rec_idx = 0
    def open_rec():
        nonlocal rec, rec_start, rec_idx
        if rec: rec.close()
        base = Path(args.record)
        path = base.with_name(f"{base.stem}_{rec_idx:03d}{base.suffix or '.csirec'}")
        rec = Recorder(str(path)); rec_start = time.time(); rec_idx += 1
        print(f"# recording -> {path}", flush=True)
    if args.record:
        open_rec()
    print(f"# presence_host listening on udp/{args.port}; adaptive-ratio detector; Ctrl-C to stop", flush=True)
    while True:
        try:
            data, _ = sock.recvfrom(4096)
        except socket.timeout:
            continue
        except KeyboardInterrupt:
            break
        now = time.time()
        if rec:
            rec.write(data, label)
            if now - rec_start >= args.rotate_s:
                open_rec()
        p = parse(data)
        if not p:
            continue
        if p["kind"] == "status":
            label = p["segment"]; rssi = p["rssi"]
            if last_uptime is not None and p["uptime_ms"] < last_uptime:
                det = Detector(); last_state = None
                print(f"{time.strftime('%H:%M:%S')}  # board rebooted: detector reset (warm-up)", flush=True)
            last_uptime = p["uptime_ms"]
            continue
        if p["kind"] != "csi":
            continue
        a, lay = amp_vector(p["iq"])
        if a is None:
            continue
        rssi = p["rssi"]
        on = det.push(a, lay, now)
        hms = time.strftime("%H:%M:%S")
        if on != last_state:
            print(f"{hms}  {'IN ' if on else 'OUT'}  jitter {det.j if not np.isnan(det.j) else 0:.3f} (x{det.rj if not np.isnan(det.rj) else 0:.1f})  wander {det.w if not np.isnan(det.w) else 0:.3f} (x{det.rw if not np.isnan(det.rw) else 0:.1f})  label {label}", flush=True)
            if args.liquid:
                rev += 1
                print(json.dumps({"type": "ExternalObservation", "route": "atech.ruview", "key": f"node/{args.node}/vitals.presence",
                                  "value": bool(on), "revision": rev, "t": round(now, 3)}), flush=True)
            last_state = on
        if now - last_status >= 5.0:
            last_status = now
            print(f"{hms}  {'in ' if on else 'out'}  j {det.j if not np.isnan(det.j) else 0:.3f}/{det.bj if not np.isnan(det.bj) else 0:.3f}  w {det.w if not np.isnan(det.w) else 0:.3f}/{det.bw if not np.isnan(det.bw) else 0:.3f}  lay {lay} states {det.n_states.get(lay, 0)} rssi {rssi} label {label}", flush=True)
        if csv and now - last_csv >= 1.0:
            last_csv = now
            csv.write(f"{now:.3f},{hms},{label},{lay},{rssi},{det.j:.5f},{det.w:.5f},{det.rj:.3f},{det.rw:.3f},{det.bj:.5f},{det.bw:.5f},{int(on)}\n"); csv.flush()
    if csv:
        csv.close()
    if rec:
        rec.close()


if __name__ == "__main__":
    main()
