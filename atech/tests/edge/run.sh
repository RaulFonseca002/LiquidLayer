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
  "$OUT" --assert "${fix[@]}"
fi
