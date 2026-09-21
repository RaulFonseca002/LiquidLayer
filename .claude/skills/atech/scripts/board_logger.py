#!/usr/bin/env python
"""board_logger.py — follow an Atech board's serial output across resets.

  board_logger.py [--log FILE] [--port /dev/ttyACM0] [--quiet]

Why this exists: on the ESP32-S3 the USB-Serial-JTAG controller resets the chip
every time a host opens the port (cannot be disabled on S3), and after a reset
Linux may re-enumerate the board under a NEW name (/dev/ttyACM0 -> /dev/ttyACM1)
while a stale handle keeps the old one alive. A plain reader therefore misses
every reboot. This logger:

  * scans for the board (/dev/ttyACM*, /dev/cu.usbmodem*), opens it with DTR/RTS
    de-asserted (the open itself still resets the board once — unavoidable),
  * timestamps every line, tags boot lines (`rst:` reason) with `## BOOT`,
  * on disconnect closes, rescans (the name may have changed) and reopens,
  * appends to --log and/or prints to stdout.

Stop with Ctrl-C. Run through atech-env.sh python (needs pyserial).
"""
from __future__ import annotations

import argparse
import glob
import sys
import time

try:
    import serial
except ImportError:  # pragma: no cover
    sys.exit("pyserial not importable; run via atech-env.sh python board_logger.py")


def find_port(preferred: str | None) -> str | None:
    cands = []
    if preferred:
        cands.append(preferred)
    cands += sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/cu.usbmodem*")) + sorted(glob.glob("/dev/tty.usbmodem*"))
    for c in cands:
        try:
            with open(c, "rb"):
                return c
        except OSError:
            continue
    return None


def follow(log_path: str | None, preferred: str | None, quiet: bool) -> None:
    log = open(log_path, "a", buffering=1) if log_path else None

    def emit(line: str) -> None:
        stamped = f"{time.strftime('%H:%M:%S')} {line}"
        if log:
            log.write(stamped + "\n")
        if not quiet:
            print(stamped, flush=True)

    emit("## logger start")
    backoff = 0.5
    while True:
        port = find_port(preferred)
        if not port:
            time.sleep(backoff)
            backoff = min(backoff * 1.5, 3.0)
            continue
        try:
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = port, 115200, 0.5
            s.dtr = False
            s.rts = False
            s.open()
        except Exception as exc:
            emit(f"## open {port} failed: {exc}")
            time.sleep(1)
            continue
        emit(f"## connected {port}")
        backoff = 0.5
        try:
            while True:
                raw = s.readline()
                if not raw:
                    continue
                line = raw.decode(errors="replace").rstrip()
                if not line:
                    continue
                if line.startswith("rst:"):
                    emit(f"## BOOT {line}")
                else:
                    emit(line)
        except Exception as exc:
            emit(f"## disconnected {port}: {str(exc)[:80]} — rescanning")
            try:
                s.close()
            except Exception:
                pass
            time.sleep(0.5)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", default=None, help="append lines to this file")
    ap.add_argument("--port", default=None, help="preferred port; auto-scan if absent or gone")
    ap.add_argument("--quiet", action="store_true", help="do not echo to stdout")
    args = ap.parse_args()
    try:
        follow(args.log, args.port, args.quiet)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
