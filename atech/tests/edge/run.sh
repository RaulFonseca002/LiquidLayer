#!/usr/bin/env bash
# Build and run the native RuViewEdge tests. Synthetic checks always; every .csirec under
# ../fixtures is replayed with acceptance gates when present.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MOD="$HERE/../../modules/ruview_csi"
OUT="${TMPDIR:-/tmp}/test_ruview_edge"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I"$MOD" "$HERE/test_ruview_edge.cpp" "$MOD/ruview_edge.cpp" -o "$OUT" -lm
"$OUT" "$@"
shopt -s nullglob
fix=("$HERE"/../fixtures/*.csirec)
if [ ${#fix[@]} -gt 0 ] && [ $# -eq 0 ]; then
  # fixtures listed in fixtures/gates.txt are acceptance gates (exit 1 on failure); the others are
  # replayed for information only (e.g. protocol 2 contains two genuine channel bursts 30 s and 90 s
  # after the owner left the room — a through-wall event, not a regression)
  gated=(); info=()
  for f in "${fix[@]}"; do
    if grep -qx "$(basename "$f")" "$HERE/../fixtures/gates.txt" 2>/dev/null; then gated+=("$f"); else info+=("$f"); fi
  done
  [ ${#info[@]} -gt 0 ] && "$OUT" "${info[@]}" || true
  [ ${#gated[@]} -gt 0 ] && "$OUT" --assert "${gated[@]}"
fi
