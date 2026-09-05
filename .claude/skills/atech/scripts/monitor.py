#!/usr/bin/env python
"""monitor.py — read events from a flashed Atech board for a bounded time.

  monitor.py [--port /dev/ttyACM0] [--seconds 8] [--max-lines 40] [--key k1,k2] [--send KEY VALUE]...

Cross-platform replacement for `timeout N atech monitor` (macOS has no
`timeout`). Prints one JSON line per event, then a one-line summary.
Read-only apart from the reset pulse the serial open may cause.
"""
from __future__ import annotations

import argparse
import json
import sys
import time

try:
    import atech
except ImportError:  # pragma: no cover
    sys.exit("atech SDK not importable; run via atech-env.sh python monitor.py")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None, help="serial port (auto-detect if omitted)")
    ap.add_argument("--seconds", type=float, default=8.0)
    ap.add_argument("--max-lines", type=int, default=40)
    ap.add_argument("--key", default=None, help="comma-separated event keys to keep")
    ap.add_argument("--send", nargs=2, action="append", metavar=("KEY", "VALUE"), default=[],
                    help="send an action right after connecting, then keep listening (repeatable). "
                         "Acks arrive on this same connection, unlike `atech send` which closes the port.")
    args = ap.parse_args()
    keys = {k.strip() for k in args.key.split(",")} if args.key else None

    deadline = time.monotonic() + args.seconds
    shown = 0
    seen: dict[str, int] = {}
    try:
        with atech.Board.connect(port=args.port) as board:
            print(f"# listening on {getattr(board.transport, 'port', args.port or 'auto')} for {args.seconds:g}s", file=sys.stderr)
            for key, raw in args.send:
                try:
                    value = json.loads(raw)
                except json.JSONDecodeError:
                    value = raw
                board.send(key, value)
                print(f"# sent {key} = {raw}", file=sys.stderr)
            while time.monotonic() < deadline and shown < args.max_lines:
                remaining = max(0.05, deadline - time.monotonic())
                ev = board.transport.recv(timeout=min(1.0, remaining))
                if ev is None:
                    continue
                key = getattr(ev, "key", None)
                if keys and key not in keys:
                    continue
                seen[key] = seen.get(key, 0) + 1
                payload = ev.model_dump() if hasattr(ev, "model_dump") else vars(ev)
                print(json.dumps(payload, default=str))
                shown += 1
    except Exception as exc:  # NoBoardFoundError, SerialException, ...
        print(f"# monitor error: {type(exc).__name__}: {exc}", file=sys.stderr)
        sys.exit(1)
    total = sum(seen.values())
    if total == 0:
        print("# no events received (board may need a reset, or the firmware emits nothing until an input happens)", file=sys.stderr)
    else:
        print(f"# {total} events: " + ", ".join(f"{k}x{n}" for k, n in sorted(seen.items())), file=sys.stderr)


if __name__ == "__main__":
    main()
