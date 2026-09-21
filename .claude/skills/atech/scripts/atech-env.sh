#!/usr/bin/env bash
# atech-env.sh — self-bootstrapping wrapper around the open-source Atech SDK.
#
# First call on a machine creates an isolated virtualenv under $ATECH_HOME
# (default ~/.atech) and installs the `atech` package from PyPI (PlatformIO
# comes with it). Every later call is instant. Nothing is installed globally.
#
# Usage:
#   atech-env.sh <atech subcommand> [args...]   run the SDK CLI (list-modules, build, upload, ...)
#   atech-env.sh python [args...]               run the venv's python (for the helper scripts)
#   atech-env.sh --where                        print the venv path
#   atech-env.sh --upgrade                      upgrade the SDK inside the venv
#
# Env overrides:
#   ATECH_HOME       where the venv/projects/logs live   (default: ~/.atech)
#   ATECH_SDK_SPEC   pip requirement to install          (default: atech>=1.0.0a7)
set -euo pipefail

ATECH_HOME="${ATECH_HOME:-$HOME/.atech}"
VENV="$ATECH_HOME/venv"
PKG="${ATECH_SDK_SPEC:-atech>=1.0.0a7}"

log() { echo "[atech-env] $*" >&2; }

venv_bin() {
  if [ -d "$VENV/Scripts" ]; then echo "$VENV/Scripts"; else echo "$VENV/bin"; fi
}

find_python() {
  local c
  for c in python3.13 python3.12 python3.11 python3.10 python3 python; do
    if command -v "$c" >/dev/null 2>&1 &&
       "$c" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' 2>/dev/null; then
      echo "$c"; return 0
    fi
  done
  return 1
}

install_sdk() {
  local py
  mkdir -p "$ATECH_HOME"
  if command -v uv >/dev/null 2>&1; then
    log "creating venv with uv at $VENV"
    [ -d "$VENV" ] || uv venv -q "$VENV"
    uv pip install -q --python "$(venv_bin)/python" --prerelease=allow --upgrade "$PKG"
  else
    py="$(find_python)" || { log "Python >= 3.10 (or uv) is required. Install it and retry."; exit 2; }
    log "creating venv with $py at $VENV"
    [ -d "$VENV" ] || "$py" -m venv "$VENV"
    "$(venv_bin)/python" -m pip install -q --disable-pip-version-check --upgrade pip
    "$(venv_bin)/python" -m pip install -q --disable-pip-version-check --pre --upgrade "$PKG"
  fi
  log "installed $("$(venv_bin)/atech" --version 2>/dev/null || echo atech)"
}

bootstrap() {
  if [ ! -x "$(venv_bin)/atech" ] && [ ! -f "$(venv_bin)/atech.exe" ]; then
    log "Atech SDK not found; one-time install into $VENV (needs network once)"
    install_sdk
  fi
}

case "${1:-}" in
  --where)   echo "$VENV"; exit 0 ;;
  --upgrade) install_sdk; exit 0 ;;
  "")        echo "usage: atech-env.sh <atech subcommand|python|--where|--upgrade> [args...]" >&2; exit 64 ;;
esac

bootstrap
BIN="$(venv_bin)"
# The SDK shells out to `pio`; make the venv's copy the one it finds.
export PATH="$BIN:$PATH"
export VIRTUAL_ENV="$VENV"
# PlatformIO keeps its toolchains here; default is ~/.platformio, which is fine,
# but honour an explicit override so the whole install can live in ATECH_HOME.
[ -n "${ATECH_PLATFORMIO_CORE_DIR:-}" ] && export PLATFORMIO_CORE_DIR="$ATECH_PLATFORMIO_CORE_DIR"

if [ "$1" = "python" ]; then
  shift
  exec "$BIN/python" "$@"
fi

exec "$BIN/atech" "$@"
