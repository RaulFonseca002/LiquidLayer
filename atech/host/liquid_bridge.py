#!/usr/bin/env python3
"""liquid_bridge.py — turn the Atech RuView node's UDP packets into Liquid observations.

Listens on the sink port and writes one NDJSON line per observation, shaped like
Liquid's `ExternalObservation` (include/liquid/effects/EffectTypes.hpp):

  {"sessionId": "...", "adapterRoute": "atech.ruview", "target": "node/1/vitals.heart_bpm",
   "observedValue": 72.0, "stateRevision": 17, "observedAtMs": 1757130000123}

Targets emitted (per node id N):
  node/N/alive            status seq (every status packet: main-loop liveness)
  node/N/state            "streaming" | "connected" | ...
  node/N/link.rssi        dBm
  node/N/csi.rate_hz      CSI frames per second
  node/N/vitals.presence  true/false
  node/N/vitals.fall      true/false
  node/N/vitals.heart_bpm, node/N/vitals.breathing_bpm, node/N/vitals.motion
  node/N/health.reset_reason, node/N/health.free_heap

Values other than `alive` are emitted only when they change (rates and floats
quantised, see QUANT), so a quiet room produces a quiet stream. stateRevision
is a monotonic counter per bridge run.

  python3 liquid_bridge.py [--port 5005] [--session liquid-dev] [--route atech.ruview] [--out -]
  python3 liquid_bridge.py --selftest        # no sockets: feeds synthetic packets through

This is the host half of the node/Liquid seam; the Liquid-side adapter that
consumes these lines is the next round of work (see AGENTS.md: no hardware in
core yet). Stdlib only.
"""
from __future__ import annotations

import argparse
import json
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ruview_packets import build_status, build_vitals, parse  # noqa: E402

QUANT = {"vitals.heart_bpm": 1.0, "vitals.breathing_bpm": 0.5, "vitals.motion": 0.01,
         "csi.rate_hz": 1.0, "link.rssi": 2.0, "health.free_heap": 8192.0}


class Bridge:
    def __init__(self, session: str, route: str, out, now_ms=None):
        self.session, self.route, self.out = session, route, out
        self.now_ms = now_ms or (lambda: int(time.time() * 1000))
        self.rev = 0
        self.last: dict[str, object] = {}
        self.emitted = 0

    def emit(self, target: str, value, force: bool = False) -> None:
        key = target.split("/", 2)[-1]
        if not force:
            prev = self.last.get(target)
            q = QUANT.get(key)
            if q is not None and isinstance(value, (int, float)) and isinstance(prev, (int, float)):
                if abs(float(value) - float(prev)) < q:
                    return
            elif prev == value:
                return
        self.last[target] = value
        self.rev += 1
        line = {"sessionId": self.session, "adapterRoute": self.route, "target": target,
                "observedValue": value, "stateRevision": self.rev, "observedAtMs": self.now_ms()}
        self.out.write(json.dumps(line, separators=(",", ":")) + "\n")
        self.out.flush()
        self.emitted += 1

    def handle(self, data: bytes) -> str | None:
        p = parse(data)
        if not p:
            return None
        n = f"node/{p['node']}/"
        if p["kind"] == "status":
            self.emit(n + "alive", p["seq"], force=True)
            self.emit(n + "state", p["state"])
            self.emit(n + "link.rssi", p["rssi"])
            self.emit(n + "csi.rate_hz", round(p["rate_hz"], 1))
            self.emit(n + "vitals.presence", p["presence"])
            self.emit(n + "vitals.fall", p["fall"])
            self.emit(n + "vitals.calibrating", p["calibrating"])
            self.emit(n + "vitals.heart_bpm", round(p["heart_bpm"], 1))
            self.emit(n + "vitals.breathing_bpm", round(p["breathing_bpm"], 1))
            self.emit(n + "vitals.motion", round(p["motion"], 3))
            self.emit(n + "health.reset_reason", p["reset_reason"])
            self.emit(n + "health.free_heap", p["free_heap"])
        elif p["kind"] == "vitals":
            # the DSP task's own 1 Hz packet: authoritative for vitals
            self.emit(n + "vitals.presence", p["presence"])
            self.emit(n + "vitals.fall", p["fall"])
            self.emit(n + "vitals.heart_bpm", round(p["heart_bpm"], 1))
            self.emit(n + "vitals.breathing_bpm", round(p["breathing_bpm"], 1))
            self.emit(n + "vitals.motion", round(p["motion"], 3))
        return p["kind"]


def selftest() -> None:
    import io
    out = io.StringIO()
    t = [1000]
    b = Bridge("t", "atech.ruview", out, now_ms=lambda: t[0])
    b.handle(build_status(seq=1, state="streaming", rate_hz=19.6, rssi=-48, heart_bpm=0, breathing_bpm=0))
    first = b.emitted
    assert first == 12, first                       # everything is new on the first packet
    b.handle(build_status(seq=2, state="streaming", rate_hz=19.9, rssi=-49, heart_bpm=0, breathing_bpm=0))
    assert b.emitted == first + 1, b.emitted          # only `alive` (rate/rssi within quantum)
    b.handle(build_vitals(presence=True, heart_bpm=72.4, breathing_bpm=15.0, motion=0.2))
    assert b.emitted == first + 1 + 4, b.emitted      # presence, hr, br, motion changed
    assert b.handle(b"junk") is None
    lines = [json.loads(l) for l in out.getvalue().splitlines()]
    assert lines[-1]["target"] == "node/1/vitals.motion" and lines[-1]["stateRevision"] == len(lines)
    assert set(lines[0]) == {"sessionId", "adapterRoute", "target", "observedValue", "stateRevision", "observedAtMs"}
    hr = [l for l in lines if l["target"].endswith("vitals.heart_bpm")][-1]
    assert hr["observedValue"] == 72.4, hr
    print(f"selftest ok: {len(lines)} observations, revisions monotonic, ExternalObservation keys present")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=5005)
    ap.add_argument("--session", default="liquid-dev")
    ap.add_argument("--route", default="atech.ruview")
    ap.add_argument("--out", default="-", help="NDJSON destination file, or - for stdout")
    ap.add_argument("--seconds", type=float, default=0, help="stop after N seconds (0 = run until Ctrl-C)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        selftest()
        return
    out = sys.stdout if args.out == "-" else open(args.out, "a", buffering=1)
    bridge = Bridge(args.session, args.route, out)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", args.port))
    sock.settimeout(0.5)
    print(f"# liquid_bridge: udp/{args.port} -> {args.out}  session={args.session} route={args.route}", file=sys.stderr)
    t_end = time.time() + args.seconds if args.seconds else None
    counts = {"csi": 0, "vitals": 0, "status": 0}
    try:
        while t_end is None or time.time() < t_end:
            try:
                data, _ = sock.recvfrom(4096)
            except socket.timeout:
                continue
            kind = bridge.handle(data)
            if kind:
                counts[kind] += 1
    except KeyboardInterrupt:
        pass
    print(f"# packets {counts}, observations emitted {bridge.emitted}", file=sys.stderr)


if __name__ == "__main__":
    main()
