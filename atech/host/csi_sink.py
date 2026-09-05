#!/usr/bin/env python3
"""csi_sink.py — minimal RuView-compatible UDP sink for validating the Atech node.

Listens on UDP :5005 (RuView default), parses ADR-018 CSI frames (0xC5110001)
and 32-byte vitals packets (0xC5110002), and prints a one-line summary per
second: frames/s, subcarriers, RSSI, sequence gaps, vitals if present.

  python3 csi_sink.py [--port 5005] [--seconds 60] [--dump N]

Exit code 0 if the average CSI rate over the run is >= --min-rate (default 15).
Stdlib only.
"""
from __future__ import annotations

import argparse
import socket
import struct
import sys
import time

MAGIC_CSI = 0xC5110001
MAGIC_VITALS = 0xC5110002


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=5005)
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--min-rate", type=float, default=15.0)
    ap.add_argument("--dump", type=int, default=0, help="print the first N raw headers")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", args.port))
    sock.settimeout(0.5)
    print(f"# listening on udp/{args.port} for {args.seconds:g}s")

    t_end = time.time() + args.seconds
    t_win = time.time()
    frames_win = 0
    frames_total = 0
    bad = 0
    last_seq = None
    gaps = 0
    dumped = 0
    src = None
    vitals = None
    nsub = None
    rssi = None
    while time.time() < t_end:
        try:
            data, addr = sock.recvfrom(4096)
        except socket.timeout:
            data = None
        if data:
            src = addr[0]
            if len(data) >= 4:
                magic = struct.unpack_from("<I", data, 0)[0]
            else:
                magic = 0
            if magic == MAGIC_CSI and len(data) >= 20:
                node, nant, nsub, freq, seq, r, nf = struct.unpack_from("<BBHIIbb", data, 4)
                rssi = r
                if last_seq is not None and seq != last_seq + 1:
                    gaps += 1
                last_seq = seq
                frames_win += 1
                frames_total += 1
                if dumped < args.dump:
                    dumped += 1
                    print(f"  csi node={node} ant={nant} nsub={nsub} freq={freq}MHz seq={seq} rssi={r} nf={nf} iq_bytes={len(data) - 20}")
            elif magic == MAGIC_VITALS and len(data) == 32:
                node, flags, br, hr, r, npers = struct.unpack_from("<BBHIbB", data, 4)
                motion, score, ts = struct.unpack_from("<ffI", data, 16)
                vitals = f"presence={flags & 1} fall={(flags >> 1) & 1} BR={br / 100:.1f} HR={hr / 10000:.1f} motion={motion:.3f} score={score:.3f}"
            else:
                bad += 1
        now = time.time()
        if now - t_win >= 1.0:
            rate = frames_win / (now - t_win)
            line = f"{time.strftime('%H:%M:%S')} from={src or '-'} csi={rate:5.1f}/s nsub={nsub} rssi={rssi} gaps={gaps} bad={bad}"
            if vitals:
                line += "  vitals: " + vitals
            print(line, flush=True)
            t_win = now
            frames_win = 0
    elapsed = args.seconds
    avg = frames_total / elapsed if elapsed else 0
    print(f"# total csi frames {frames_total}, average {avg:.1f}/s, gaps {gaps}, unknown packets {bad}")
    sys.exit(0 if avg >= args.min_rate else 1)


if __name__ == "__main__":
    main()
