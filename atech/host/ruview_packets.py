"""ruview_packets.py — parsers for the UDP packets the Atech RuView node sends.

Three packet kinds arrive on the sink port (default 5005):
  CSI frame    magic 0xC5110001  RuView ADR-018: 20-byte header + int8 I/Q pairs
  Vitals       magic 0xC5110002  RuView ADR-039: 32 bytes, 1 Hz, from the DSP task
  NodeStatus   magic 0xA11E0002  ours: 40 bytes, 1 Hz, sent from loop() (liveness)

parse(data) -> dict with "kind" in {"csi", "vitals", "status"} or None if unknown.
Stdlib only; shared by csi_sink.py and liquid_bridge.py.
"""
from __future__ import annotations

import struct

MAGIC_CSI = 0xC5110001
MAGIC_VITALS = 0xC5110002
MAGIC_STATUS = 0xA11E0002

STATE_NAMES = ["unconfigured", "idle", "connecting", "connected", "streaming", "lost"]
SEGMENT_NAMES = ["live", "empty", "still", "walk"]   # button protocol segments (NodeStatus flags bits 4-5)
RESET_REASONS = {0: "unknown", 1: "power-on", 2: "external", 3: "software", 4: "panic",
                 5: "int-wdt", 6: "task-wdt", 7: "wdt", 8: "deepsleep", 9: "brownout", 10: "sdio"}


def parse(data: bytes) -> dict | None:
    if len(data) < 4:
        return None
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic == MAGIC_CSI and len(data) >= 20:
        node, nant, nsub, freq, seq, rssi, noise, flags, mcs = struct.unpack_from("<BBHIIbbBB", data, 4)
        return {"kind": "csi", "node": node, "n_ant": nant, "n_sub": nsub, "freq_mhz": freq,
                "seq": seq, "rssi": rssi, "noise": noise, "iq_bytes": len(data) - 20, "iq": data[20:],
                "first_word_invalid": bool(flags & 1), "ht": bool(flags & 2), "stbc": bool(flags & 4),
                "ht40": bool(flags & 8), "sgi": bool(flags & 16), "csi_cfg": (flags >> 6) & 3, "mcs": mcs}
    if magic == MAGIC_VITALS and len(data) == 32:
        node, flags, br, hr, rssi, npers = struct.unpack_from("<BBHIbB", data, 4)
        motion, score, ts = struct.unpack_from("<ffI", data, 16)
        return {"kind": "vitals", "node": node, "presence": bool(flags & 1), "fall": bool(flags & 2),
                "motion_flag": bool(flags & 4), "breathing_bpm": br / 100.0, "heart_bpm": hr / 10000.0,
                "rssi": rssi, "n_persons": npers, "motion": motion, "presence_score": score, "ts_ms": ts}
    if magic == MAGIC_STATUS and len(data) == 40:
        node, state, flags, reset, seq, up, heap = struct.unpack_from("<BBBBIII", data, 4)
        rate, hr, br, motion = struct.unpack_from("<ffff", data, 20)
        rssi, layout = struct.unpack_from("<bB", data, 36)
        return {"kind": "status", "node": node, "state": STATE_NAMES[state] if state < len(STATE_NAMES) else str(state),
                "presence": bool(flags & 1), "fall": bool(flags & 2), "calibrating": bool(flags & 4),
                "csi_on": bool(flags & 8), "reset_reason": RESET_REASONS.get(reset, str(reset)),
                "reset_code": reset, "seq": seq, "uptime_ms": up, "free_heap": heap, "rate_hz": rate,
                "heart_bpm": hr, "breathing_bpm": br, "motion": motion, "rssi": rssi,
                "segment": (flags >> 4) & 3, "segment_name": SEGMENT_NAMES[(flags >> 4) & 3], "layout": layout}
    return None


def build_status(**kw) -> bytes:
    """Encode a NodeStatus packet (used by self-tests)."""
    flags = (1 if kw.get("presence") else 0) | (2 if kw.get("fall") else 0) | \
            (4 if kw.get("calibrating") else 0) | (8 if kw.get("csi_on", True) else 0) | \
            ((kw.get("segment", 0) & 3) << 4)
    state = STATE_NAMES.index(kw.get("state", "streaming"))
    return struct.pack("<IBBBBIIIffffbB2x", MAGIC_STATUS, kw.get("node", 1), state, flags,
                       kw.get("reset_code", 1), kw.get("seq", 0), kw.get("uptime_ms", 0),
                       kw.get("free_heap", 200000), kw.get("rate_hz", 0.0), kw.get("heart_bpm", 0.0),
                       kw.get("breathing_bpm", 0.0), kw.get("motion", 0.0), kw.get("rssi", -50),
                       kw.get("layout", 0))


def build_vitals(**kw) -> bytes:
    flags = (1 if kw.get("presence") else 0) | (2 if kw.get("fall") else 0)
    return struct.pack("<IBBHIbBxxffII", MAGIC_VITALS, kw.get("node", 1), flags,
                       int(round(kw.get("breathing_bpm", 0.0) * 100)), int(round(kw.get("heart_bpm", 0.0) * 10000)),
                       kw.get("rssi", -50), kw.get("n_persons", 0), kw.get("motion", 0.0),
                       kw.get("presence_score", 0.0), kw.get("ts_ms", 0), 0)
