#!/usr/bin/env python3
"""csi_sink.py — minimal RuView-compatible UDP sink for validating the Atech node.

Listens on UDP :5005 (RuView default), parses ADR-018 CSI frames (0xC5110001)
32-byte vitals packets (0xC5110002) and 40-byte node-status beacons (0xA11E0002), and prints a one-line summary per
second: frames/s, subcarriers, RSSI, sequence gaps, vitals if present.

  python3 csi_sink.py [--port 5005] [--seconds 60] [--dump N] [--record FILE.csirec]

--record writes every packet to a .csirec file (see csirec.py) with the host arrival
time and the current button segment (from the latest NodeStatus), for offline analysis
and replay tests. --seconds 0 runs until Ctrl-C.

Exit code 0 if the average CSI rate over the run is >= --min-rate (default 15).
Stdlib only.
"""
from __future__ import annotations

import argparse
import socket
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ruview_packets import parse  # noqa: E402
from csirec import Recorder  # noqa: E402

MAGIC_CSI = 0xC5110001
MAGIC_VITALS = 0xC5110002
MAGIC_STATUS = 0xA11E0002  # node status beacon, 40 bytes, 1 Hz, sent from loop() (see ruview_packets.py)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=5005)
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--min-rate", type=float, default=15.0)
    ap.add_argument("--dump", type=int, default=0, help="print the first N raw headers")
    ap.add_argument("--record", type=str, default="", help="write all packets to this .csirec file")
    args = ap.parse_args()
    rec = Recorder(args.record) if args.record else None

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", args.port))
    sock.settimeout(0.5)
    print(f"# listening on udp/{args.port} for {args.seconds:g}s" + (f", recording to {args.record}" if rec else ""))

    t_start = time.time()
    t_end = t_start + args.seconds if args.seconds > 0 else float("inf")
    segment = 0
    segment_name = "live"
    t_win = time.time()
    frames_win = 0
    frames_total = 0
    bad = 0
    last_seq = None
    gaps = 0
    dumped = 0
    src = None
    vitals = None
    status = None
    nsub = None
    rssi = None
    while time.time() < t_end:
        try:
            data, addr = sock.recvfrom(4096)
        except socket.timeout:
            data = None
        except KeyboardInterrupt:
            break
        if data:
            src = addr[0]
            if rec:
                rec.write(data, segment)
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
            elif magic == MAGIC_STATUS and len(data) == 40:
                st = parse(data)
                segment, segment_name = st["segment"], st["segment_name"]
                status = (f"STATUS seq={st['seq']} {st['state']} up={st['uptime_ms']/1000:.0f}s heap={st['free_heap']//1024}K "
                          f"reset={st['reset_reason']} rate={st['rate_hz']:.1f} seg={segment_name} calib={int(st['calibrating'])} "
                          f"presence={int(st['presence'])} layout={st['layout']}")
                if st["reset_code"] == 6:
                    status += "  <-- TASK-WDT REBOOT (a masked hang)"
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
            if status:
                line += "  " + status
            print(line, flush=True)
            t_win = now
            frames_win = 0
    if rec:
        rec.close()
        print(f"# recorded {rec.count} packets to {args.record}")
    elapsed = time.time() - t_start
    avg = frames_total / elapsed if elapsed else 0
    print(f"# total csi frames {frames_total}, average {avg:.1f}/s, gaps {gaps}, unknown packets {bad}")
    sys.exit(0 if avg >= args.min_rate else 1)


if __name__ == "__main__":
    main()
