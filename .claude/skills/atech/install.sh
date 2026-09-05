#!/usr/bin/env bash
# install.sh — copy this skill into Claude Code's global skills folder.
# Usage: ./install.sh            -> ~/.claude/skills/atech
#        ./install.sh <project>  -> <project>/.claude/skills/atech
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ $# -ge 1 ]; then DEST="$1/.claude/skills/atech"; else DEST="$HOME/.claude/skills/atech"; fi
mkdir -p "$(dirname "$DEST")"
rm -rf "$DEST"
cp -R "$HERE" "$DEST"
find "$DEST" -name '__pycache__' -type d -prune -exec rm -rf {} +
chmod +x "$DEST"/scripts/*.sh "$DEST"/scripts/*.py "$DEST/install.sh"
echo "installed /atech skill to $DEST"
echo "requirements: Python 3.10+ (or uv), g++ for the simulator, an Atech board on USB"
echo "now open Claude Code and type:  /atech <what you want the board to do>"
