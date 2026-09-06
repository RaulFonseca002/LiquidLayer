"""dataset.py — labelled CSI dataset loader (analysis tooling; numpy allowed, not part of the tests).

Reads .csirec recordings (host/csirec.py format) and the manifest sessions.json, and returns per
frame: time (s from file start), layout (bytes/128), rssi, frame flags/mcs (0 for recordings made
before the flags existed), the 52-bin HT-LTF amplitude vector (L2-normalised, same bins as the
firmware), and a label id per frame from the manifest windows.

    from analysis.dataset import load_manifest, load_session, LABELS
    man = load_manifest()
    ses = load_session(man["sessions"][0])
    ses.t, ses.layout, ses.rssi, ses.amp (N x 52), ses.label (N,), ses.windows

Labels: 0 unknown, 1 out, 2 still, 3 walk, 4 live(unknown occupancy), 5 leave, 6 enter.
"""
from __future__ import annotations

import json
import os
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "host"))
import csirec  # noqa: E402

LABELS = {"unknown": 0, "out": 1, "still": 2, "walk": 3, "live": 4, "leave": 5, "enter": 6}
LABEL_NAMES = {v: k for k, v in LABELS.items()}

PILOTS = {7, 21, 43, 57}
HT_BINS = np.array([i for i in list(range(1, 29)) + list(range(36, 64)) if i not in PILOTS], dtype=np.int64)  # 52
LLTF_BINS = np.array([i for i in list(range(1, 27)) + list(range(38, 64)) if i not in PILOTS], dtype=np.int64)  # 48


@dataclass
class Session:
    path: str
    t: np.ndarray            # seconds from the first record
    t_us: np.ndarray         # absolute epoch microseconds
    layout: np.ndarray       # bytes/128: 1 LLTF only, 2 HT, 3 HT+STBC
    rssi: np.ndarray
    seq: np.ndarray
    flags: np.ndarray        # header byte 18 (0 for old recordings)
    mcs: np.ndarray          # header byte 19
    amp: np.ndarray          # N x 52 normalised HT-LTF amplitudes (NaN rows for LLTF-only frames)
    amp_lltf: np.ndarray     # N x 48 normalised LLTF amplitudes (all frames have an LLTF block)
    button: np.ndarray       # button label at arrival (0 live, 1 out, 2 still, 3 walk)
    label: np.ndarray        # manifest label id per frame
    windows: list = field(default_factory=list)
    meta: dict = field(default_factory=dict)

    def mask(self, *names: str) -> np.ndarray:
        ids = [LABELS[n] for n in names]
        return np.isin(self.label, ids)


def load_manifest(path: str | None = None) -> dict:
    p = Path(path) if path else HERE / "sessions.json"
    return json.loads(p.read_text())


def _amplitudes(iq: np.ndarray, base: int, bins: np.ndarray) -> np.ndarray:
    """iq: N x L int8; returns N x len(bins) L2-normalised amplitudes of block `base` (0 LLTF, 128 HT-LTF)."""
    im = iq[:, base + 2 * bins].astype(np.float32)
    re = iq[:, base + 2 * bins + 1].astype(np.float32)
    a = np.sqrt(im * im + re * re)
    n = np.linalg.norm(a, axis=1, keepdims=True)
    n[n < 1e-6] = np.nan
    return a / n


def load_session(entry: dict, max_frames: int | None = None) -> Session:
    path = os.path.expanduser(entry["file"])
    recs = []
    for kind, t_us, seg, payload in csirec.read(path):
        if kind != csirec.KIND_CSI or len(payload) < 20:
            continue
        recs.append((t_us, seg, payload))
        if max_frames and len(recs) >= max_frames:
            break
    if not recs:
        raise ValueError(f"{path}: no CSI frames")
    n = len(recs)
    t_us = np.array([r[0] for r in recs], dtype=np.int64)
    button = np.array([r[1] for r in recs], dtype=np.int8)
    hdr = np.array([struct.unpack_from("<BBHIIbbBB", r[2], 4) for r in recs])
    seq = hdr[:, 4].astype(np.int64)
    rssi = hdr[:, 5].astype(np.int8).astype(np.int16)
    flags = hdr[:, 7].astype(np.uint8)
    mcs = hdr[:, 8].astype(np.uint8)
    lens = np.array([len(r[2]) - 20 for r in recs])
    layout = (lens // 128).astype(np.int8)
    maxlen = int(lens.max())
    iq = np.zeros((n, max(maxlen, 128)), dtype=np.int8)
    for i, r in enumerate(recs):
        b = np.frombuffer(r[2], dtype=np.int8, offset=20)
        iq[i, :len(b)] = b
    amp = np.full((n, len(HT_BINS)), np.nan, dtype=np.float32)
    has_ht = lens >= 256
    if has_ht.any():
        amp[has_ht] = _amplitudes(iq[has_ht], 128, HT_BINS)
    amp_lltf = _amplitudes(iq, 0, LLTF_BINS)
    t = (t_us - t_us[0]) / 1e6
    label = np.zeros(n, dtype=np.int8)
    for w in entry.get("windows", []):
        m = (t >= w["t0"]) & (t < w["t1"])
        label[m] = LABELS[w["label"]]
    return Session(path=path, t=t, t_us=t_us, layout=layout, rssi=rssi, seq=seq, flags=flags, mcs=mcs,
                   amp=amp, amp_lltf=amp_lltf, button=button, label=label, windows=entry.get("windows", []),
                   meta={k: v for k, v in entry.items() if k != "windows"})


def summary(ses: Session) -> str:
    lines = [f"{ses.path}: {len(ses.t)} frames, {ses.t[-1]:.0f} s, layouts {dict(zip(*np.unique(ses.layout, return_counts=True)))}"]
    for lid in sorted(set(ses.label.tolist())):
        m = ses.label == lid
        dur = float(np.diff(np.where(m)[0]).clip(max=1).sum()) if m.sum() > 1 else 0.0
        secs = len(np.unique(np.floor(ses.t[m]))) if m.any() else 0
        lines.append(f"  {LABEL_NAMES[lid]:8s} {m.sum():6d} frames  ~{secs:5d} s  rssi {ses.rssi[m].mean():.0f} dBm")
    return "\n".join(lines)


if __name__ == "__main__":
    man = load_manifest(sys.argv[1] if len(sys.argv) > 1 else None)
    for e in man["sessions"]:
        print(summary(load_session(e)))
