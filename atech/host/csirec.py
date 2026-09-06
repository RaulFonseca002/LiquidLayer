#!/usr/bin/env python3
"""csirec.py — the .csirec recording format shared by csi_sink.py, edge_proto.py and the
native replay test (atech/tests/edge).

File = 8-byte magic b"CSIREC01" then records:
    <B  kind      1 csi frame, 2 vitals, 3 node status, 0 unknown  (by UDP magic)
    <q  t_us      host arrival time, microseconds since the Unix epoch
    <B  segment   button segment at arrival (0 live, 1 empty, 2 still, 3 walk), from the latest NodeStatus
    <H  n         payload length
    n bytes       the raw UDP payload (parse with ruview_packets.parse)

Little-endian, no compression, ~400 bytes per CSI frame at 192 subcarriers, so 6 minutes at
20 Hz is under 3 MB. Stdlib only.

    python3 csirec.py FILE.csirec            # summary per segment
    python3 csirec.py FILE.csirec --trim OUT --keep 90   # first 90 s of every protocol segment run (>= 30 s), +30 s of the final live
"""
from __future__ import annotations

import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ruview_packets import MAGIC_CSI, MAGIC_STATUS, MAGIC_VITALS, SEGMENT_NAMES, parse  # noqa: E402

MAGIC_FILE = b"CSIREC01"
HDR = struct.Struct("<BqBH")
KIND_CSI, KIND_VITALS, KIND_STATUS = 1, 2, 3


def kind_of(payload: bytes) -> int:
    if len(payload) < 4:
        return 0
    magic = struct.unpack_from("<I", payload, 0)[0]
    return {MAGIC_CSI: KIND_CSI, MAGIC_VITALS: KIND_VITALS, MAGIC_STATUS: KIND_STATUS}.get(magic, 0)


class Recorder:
    def __init__(self, path: str):
        self._f = open(path, "wb")
        self._f.write(MAGIC_FILE)
        self.count = 0

    def write(self, payload: bytes, segment: int, t_us: int | None = None) -> None:
        if t_us is None:
            t_us = time.time_ns() // 1000
        self._f.write(HDR.pack(kind_of(payload), t_us, segment & 3, len(payload)))
        self._f.write(payload)
        self.count += 1

    def close(self) -> None:
        self._f.close()


def read(path: str):
    """Yield (kind, t_us, segment, payload) for every record."""
    data = Path(path).read_bytes()
    if data[:8] != MAGIC_FILE:
        raise ValueError(f"{path}: not a .csirec file")
    pos = 8
    while pos + HDR.size <= len(data):
        kind, t_us, seg, n = HDR.unpack_from(data, pos)
        pos += HDR.size
        yield kind, t_us, seg, data[pos:pos + n]
        pos += n


def csi_frames(path: str):
    """Yield (t_s, segment, rssi, iq_bytes) for CSI records; t_s is seconds from the first record."""
    t0 = None
    for kind, t_us, seg, payload in read(path):
        if t0 is None:
            t0 = t_us
        if kind != KIND_CSI:
            continue
        p = parse(payload)
        if p:
            yield (t_us - t0) / 1e6, seg, p["rssi"], p["iq"]


def summary(path: str) -> str:
    per = {}
    first = last = None
    for t_s, seg, rssi, iq in csi_frames(path):
        d = per.setdefault(seg, {"n": 0, "t0": t_s, "t1": t_s, "lens": {}, "rssi": 0})
        d["n"] += 1
        d["t1"] = t_s
        d["lens"][len(iq)] = d["lens"].get(len(iq), 0) + 1
        d["rssi"] += rssi
        first = t_s if first is None else first
        last = t_s
    lines = [f"{path}: {sum(d['n'] for d in per.values())} CSI frames over {(last or 0) - (first or 0):.1f} s"]
    for seg in sorted(per):
        d = per[seg]
        dur = max(d["t1"] - d["t0"], 1e-9)
        lens = ",".join(f"{k}B×{v}" for k, v in sorted(d["lens"].items()))
        lines.append(f"  seg {seg} {SEGMENT_NAMES[seg]:5s} {d['n']:6d} frames  {dur:6.1f} s  {d['n']/dur:5.1f} fps  "
                     f"rssi {d['rssi']/d['n']:.0f} dBm  iq {lens}")
    return "\n".join(lines)


def trim(src: str, dst: str, keep_s: float, min_run_s: float = 30.0, live_keep_s: float = 30.0) -> int:
    """Copy src to dst keeping, for every contiguous run of a non-live segment longer than
    min_run_s (shorter runs are button false starts), its first keep_s seconds, plus the first
    live_keep_s seconds of the live run that follows the protocol. Frames keep their timestamps."""
    recs = list(read(src))
    runs = []            # (seg, start_idx, end_idx_exclusive, t0_us, t1_us)
    for i, (kind, t_us, seg, payload) in enumerate(recs):
        if runs and runs[-1][0] == seg:
            runs[-1][2] = i + 1
            runs[-1][4] = t_us
        else:
            runs.append([seg, i, i + 1, t_us, t_us])
    keep = set()
    last_protocol_end = None
    for seg, a, b, t0, t1 in runs:
        dur = (t1 - t0) / 1e6
        if seg != 0 and dur >= min_run_s:
            for i in range(a, b):
                if (recs[i][1] - t0) / 1e6 <= keep_s:
                    keep.add(i)
            last_protocol_end = b
    if last_protocol_end is not None:
        for seg, a, b, t0, t1 in runs:
            if a == last_protocol_end and seg == 0:
                for i in range(a, b):
                    if (recs[i][1] - t0) / 1e6 <= live_keep_s:
                        keep.add(i)
    out = Recorder(dst)
    for i in sorted(keep):
        kind, t_us, seg, payload = recs[i]
        out.write(payload, seg, t_us)
    out.close()
    return out.count


def main(argv: list[str]) -> None:
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return
    path = argv[0]
    if "--trim" in argv:
        dst = argv[argv.index("--trim") + 1]
        keep = float(argv[argv.index("--keep") + 1]) if "--keep" in argv else 90.0
        n = trim(path, dst, keep)
        print(f"wrote {n} records to {dst}")
        print(summary(dst))
    else:
        print(summary(path))


if __name__ == "__main__":
    main(sys.argv[1:])
