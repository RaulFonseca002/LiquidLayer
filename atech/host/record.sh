#!/usr/bin/env bash
# Long-running CSI recorder with hourly file rotation: ~/.atech/recordings/<date>/HHMMSS.csirec
# plus a status log next to it. Ground truth: the board button (labels ride in the beacons) and
# `python3 atech/host/note.py "text"` for timestamped notes. Stop with Ctrl-C or `pkill -f record.sh`.
#   atech/host/record.sh [--hours N] [--port 5005]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
HOURS=1000; PORT=5005
while [ $# -gt 0 ]; do case "$1" in --hours) HOURS="$2"; shift 2;; --port) PORT="$2"; shift 2;; *) echo "usage: record.sh [--hours N] [--port P]" >&2; exit 64;; esac; done
n=0
while [ "$n" -lt "$HOURS" ]; do
  d="$HOME/.atech/recordings/$(date +%Y%m%d)"; mkdir -p "$d"
  f="$d/$(date +%H%M%S).csirec"
  echo "$(date +%F_%T) recording -> $f"
  python3 "$HERE/csi_sink.py" --port "$PORT" --seconds 3600 --record "$f" >> "$d/sink.log" 2>&1 || true
  n=$((n + 1))
done
