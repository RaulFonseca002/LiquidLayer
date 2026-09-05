#!/usr/bin/env bash
# deploy.sh — validate -> build -> flash -> listen, for one Atech project.
#
# Usage: deploy.sh <project-dir> [--port /dev/ttyACM0] [--monitor SECONDS] [--no-upload] [--no-build]
#
# Prints a short, agent-readable summary. Full tool output goes to
# $ATECH_HOME/logs/<project>-<timestamp>.log (path printed at the end).
# Exit code is non-zero on the first failing step, with the log tail shown.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV="$HERE/atech-env.sh"
ATECH_HOME="${ATECH_HOME:-$HOME/.atech}"

PROJECT=""; PORT=""; MONITOR=8; DO_UPLOAD=1; DO_BUILD=1
while [ $# -gt 0 ]; do
  case "$1" in
    --port)      PORT="$2"; shift 2 ;;
    --monitor)   MONITOR="$2"; shift 2 ;;
    --no-upload) DO_UPLOAD=0; shift ;;
    --no-build)  DO_BUILD=0; shift ;;
    -h|--help)   sed -n 2,9p "$0"; exit 0 ;;
    *)           PROJECT="$1"; shift ;;
  esac
done
[ -n "$PROJECT" ] || { echo "usage: deploy.sh <project-dir> [--port P] [--monitor S] [--no-upload]" >&2; exit 64; }
[ -f "$PROJECT/project.yaml" ] || { echo "no project.yaml in $PROJECT" >&2; exit 66; }

NAME="$(basename "$(cd "$PROJECT" && pwd)")"
mkdir -p "$ATECH_HOME/logs"
LOG="$ATECH_HOME/logs/$NAME-$(date +%Y%m%d-%H%M%S).log"

step() {  # step <label> <cmd...>  -> runs, appends to log, prints status
  local label="$1"; shift
  local t0=$(date +%s)
  echo "=== $label: $*" >>"$LOG"
  local rc=0
  "$@" >>"$LOG" 2>&1 || rc=$?
  if [ "$rc" -eq 0 ]; then
    echo "[ok]   $label ($(( $(date +%s)-t0 ))s)"
    return 0
  fi
  echo "[FAIL] $label (rc=$rc). Last lines:"
  tail -n 25 "$LOG" | sed 's/^/       /'
  hint "$LOG"
  echo "log: $LOG"
  exit $rc
}

hint() {  # common failure hints, read from the log
  if grep -qiE 'permission denied.*tty|could not open port' "$1"; then
    echo "       hint: serial permission. Linux: sudo usermod -aG dialout \$USER, then log out/in (or: sudo chmod a+rw <port>)."
  fi
  if grep -qiE 'resource busy|device or resource busy|port is busy' "$1"; then
    echo "       hint: another program holds the port. Close the Atech dashboard / serial monitor, or run: atech-env.sh free --port <port>"
  fi
  if grep -qiE 'no board found|no candidate' "$1"; then
    echo "       hint: no Atech board detected. Plug it in over USB-C (data cable, not charge-only) and rerun atech-env.sh ports."
  fi
}

echo "project: $NAME  ($PROJECT)"

# 0. board present?
if [ -z "$PORT" ]; then
  PORTS_OUT="$("$ENV" ports 2>&1 || true)"
  echo "$PORTS_OUT" >>"$LOG"
  N=$(echo "$PORTS_OUT" | grep -c '^/dev\|^COM\|^tty' || true)
  if [ "$N" -eq 1 ]; then
    PORT="$(echo "$PORTS_OUT" | grep -m1 '^/dev\|^COM\|^tty' | awk '{print $1}')"
  elif [ "$N" -gt 1 ] && [ "$DO_UPLOAD" -eq 1 ]; then
    echo "[STOP] several serial devices found; pass --port explicitly:"; echo "$PORTS_OUT" | sed 's/^/       /'; exit 65
  elif [ "$DO_UPLOAD" -eq 1 ]; then
    echo "[STOP] no Atech board found on USB. Connect it and retry (or use --no-upload to just build)."; exit 65
  fi
fi
[ -n "$PORT" ] && echo "board:   $PORT"

# 1. validate
step "validate" "$ENV" validate "$PROJECT"

# 2. build
if [ "$DO_BUILD" -eq 1 ]; then
  step "build   " "$ENV" build "$PROJECT"
  BIN="$(grep -oE 'firmware: .*firmware\.bin' "$LOG" | tail -1 | cut -d' ' -f2-)"
  [ -n "$BIN" ] && echo "       firmware: $BIN ($(du -h "$BIN" 2>/dev/null | cut -f1))"
fi

# 3. upload
if [ "$DO_UPLOAD" -eq 1 ]; then
  step "upload  " "$ENV" upload "$PROJECT" --port "$PORT"
fi

# 4. listen
if [ "$DO_UPLOAD" -eq 1 ] && [ "$MONITOR" != "0" ]; then
  echo "--- events for ${MONITOR}s after flash ---"
  "$ENV" python "$HERE/monitor.py" --port "$PORT" --seconds "$MONITOR" --max-lines 30 2>&1 | tee -a "$LOG" | head -40
fi

echo "log: $LOG"
