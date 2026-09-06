#!/usr/bin/env python3
"""note.py — append a timestamped ground-truth note to today's recording folder.

    python3 atech/host/note.py "left the room"          # wall-clock timestamp now
    python3 atech/host/note.py --at 22:41 "going to sleep, room empty until ~07:00"

Notes land in ~/.atech/recordings/<date>/notes.ndjson as {"t": epoch_seconds, "hms": ..., "text": ...}
and are picked up by analysis/dataset.py as label hints (they never override button labels).
"""
import json, sys, time, datetime
from pathlib import Path

def main(argv):
    at = None
    if argv and argv[0] == "--at":
        hh, mm = argv[1].split(":")[:2]
        now = datetime.datetime.now()
        at = now.replace(hour=int(hh), minute=int(mm), second=0, microsecond=0).timestamp()
        argv = argv[2:]
    text = " ".join(argv).strip()
    if not text:
        print(__doc__); return
    t = at if at is not None else time.time()
    d = Path.home() / ".atech" / "recordings" / datetime.datetime.fromtimestamp(t).strftime("%Y%m%d")
    d.mkdir(parents=True, exist_ok=True)
    rec = {"t": round(t, 3), "hms": datetime.datetime.fromtimestamp(t).strftime("%H:%M:%S"), "text": text}
    with open(d / "notes.ndjson", "a") as f:
        f.write(json.dumps(rec) + "\n")
    print(f"noted {rec['hms']}: {text}")

if __name__ == "__main__":
    main(sys.argv[1:])
