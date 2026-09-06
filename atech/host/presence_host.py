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
     "template_frames": 300, "warmup_s": 15.0}


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
        self.prev = {}; self.prev_t = {}
        self.tpl = {}; self.tpl_n = {}
        self.bj = np.nan; self.bw = np.nan
        self.hist = []; self.last_event = -1e9; self.on = False; self.on_since = 0.0
        self.last_t = None
        self.t0 = None   # first frame time: no events during the warm-up, baselines learn from medians
        self.warm_j = []; self.warm_w = []
        self.rj = np.nan; self.rw = np.nan; self.j = np.nan; self.w = np.nan

    def push(self, a, lay, t):
        p = self.p
        dt = (t - self.last_t) if self.last_t is not None else 0.05
        if dt > 5.0:
            self.bj = self.bw = np.nan
        self.last_t = t
        # jitter against the previous frame of the same layout (within 1 s)
        j = np.nan
        if lay in self.prev and t - self.prev_t[lay] <= 1.0:
            j = max(0.0, 1.0 - corr(a, self.prev[lay]))
        self.prev[lay] = a; self.prev_t[lay] = t
        # per-layout template: bootstrap from the first frames of that layout, then learn only while absent and quiet
        w = np.nan
        if lay not in self.tpl:
            self.tpl[lay] = a.copy(); self.tpl_n[lay] = 1
        else:
            T = self.tpl[lay]
            if self.tpl_n[lay] >= 30:
                w = max(0.0, 1.0 - corr(a, T))
            self.tpl_n[lay] += 1
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
        if sum(self.hist) >= p["k"]:
            self.last_event = t
        moved = (t - self.last_event) <= p["hold_s"]
        remembered = (t - self.last_event) <= p["memory_s"]
        still_body = (not np.isnan(rw)) and rw > p["R_w"] and remembered
        new_on = moved or still_body
        if new_on and not self.on:
            self.on_since = t
        self.on = new_on
        # baselines and template tracking
        quiet = (not np.isnan(rj)) and rj < 2.0
        tau = p["tau_fast_s"] if (not self.on and quiet) else p["tau_slow_s"]
        g = min(1.0, dt / tau)
        if not np.isnan(j): self.bj += g * (j - self.bj)
        if not np.isnan(w) and not self.on: self.bw += g * (w - self.bw)   # wander baseline only while absent
        n = self.tpl_n[lay]
        if n < p["template_frames"]:
            gt = 1.0 / n
        elif not self.on and quiet:
            gt = min(1.0, dt / p["tau_template_s"])
        else:
            gt = 0.0
        if gt > 0:
            T = self.tpl[lay] + gt * (a - self.tpl[lay]); self.tpl[lay] = T / np.linalg.norm(T)
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
    label = 0; rssi = 0; last_status = 0.0; last_csv = 0.0; last_state = None; rev = 0
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
            print(f"{hms}  {'in ' if on else 'out'}  j {det.j if not np.isnan(det.j) else 0:.3f}/{det.bj if not np.isnan(det.bj) else 0:.3f}  w {det.w if not np.isnan(det.w) else 0:.3f}/{det.bw if not np.isnan(det.bw) else 0:.3f}  lay {lay} rssi {rssi} label {label}", flush=True)
        if csv and now - last_csv >= 1.0:
            last_csv = now
            csv.write(f"{now:.3f},{hms},{label},{lay},{rssi},{det.j:.5f},{det.w:.5f},{det.rj:.3f},{det.rw:.3f},{det.bj:.5f},{det.bw:.5f},{int(on)}\n"); csv.flush()
    if csv:
        csv.close()
    if rec:
        rec.close()


if __name__ == "__main__":
    main()
