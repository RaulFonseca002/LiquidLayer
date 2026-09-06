#!/usr/bin/env python3
"""build_flash.py — generate → harden → compile → flash one Atech project.

Replaces `atech build` / `atech upload` in the skill pipeline so the generated
tree can be patched between generation and compilation:

  1. purge stale generated code (build/lib, build/src). The SDK does not clear
     them, so a renamed or removed module lingers and the linker can bind the
     wrong object (two modules defining one class → `undefined reference`, or
     silently wrong code). The PlatformIO cache (build/.pio) is kept.
  2 + 3. fix the frozen-board bug. Its true cause is the SDK's generated
     `Serial.setTxTimeoutMs(0)`: the ESP32-S3 USB write loop seeds its
     unplug-detection counter from that timeout, and 0 makes it underflow so the
     loop spins forever the moment the board writes with no host reading (buffer
     fills, never drains). Fix: set ARDUINO_USB_CDC_ON_BOOT=0 and route the
     generated `Serial` through atechUsb() (atech_usb.h), which brings the USB-CDC
     up from loop() ~2 s after boot AND sets a bounded non-zero TX timeout (the
     operative fix). USB events keep working; UART0 (port-11 pins) never starts.
     Proof and full story: atech/STAGES.md.

Usage:
  build_flash.py <project-dir> [--no-upload | --port /dev/ttyACM0] [--stock-usb]
  build_flash.py <project-dir> --upload-only --port /dev/ttyACM0

Prints `hardened: ...` and `firmware: <path>` (deploy.sh reads both).
Exit code is non-zero on the first failure, with the tool output tail.
"""
from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

import atech
from atech.build import run_build
from atech.upload import run_upload

HERE = Path(__file__).resolve().parent
USB_HDR = HERE / "atech_usb.h"


def harden(build: Path) -> list[str]:
    notes: list[str] = []
    ini = build / "platformio.ini"
    text = ini.read_text()
    if "-DARDUINO_USB_CDC_ON_BOOT=1" in text:
        ini.write_text(text.replace("-DARDUINO_USB_CDC_ON_BOOT=1", "-DARDUINO_USB_CDC_ON_BOOT=0"))
        notes.append("CDC_ON_BOOT=0")

    main = build / "src" / "main.cpp"
    src = main.read_text()
    if "atech_usb.h" not in src:
        shutil.copy(USB_HDR, build / "src" / "atech_usb.h")
        lines = src.split("\n")
        includes = [i for i, l in enumerate(lines) if l.startswith("#include")]
        if not includes:
            sys.exit("build_flash: generated main.cpp has no #include lines")
        at = includes[-1] + 1
        lines[at:at] = [
            '#include "atech_usb.h"',
            "#define Serial atechUsb()   // deferred USB-CDC (atech_usb.h); UART0 never starts",
        ]
        src = "\n".join(lines)
        patched = re.sub(r"(void loop\(\)\s*\{\n)", r"\1    atechUsb().update();   // bring USB-CDC up once the boot has settled\n", src, count=1)
        if patched == src:
            sys.exit("build_flash: could not find `void loop() {` in generated main.cpp")
        main.write_text(patched)
        notes.append("Serial->atechUsb()")
    return notes


def tail(s: str, n: int = 3000) -> str:
    return s[-n:] if s else ""


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("project")
    ap.add_argument("--port")
    ap.add_argument("--no-upload", action="store_true")
    ap.add_argument("--upload-only", action="store_true")
    ap.add_argument("--stock-usb", action="store_true", help="skip the USB hardening (reproduce the SDK default build)")
    args = ap.parse_args()

    pdir = Path(args.project).resolve()
    yaml = pdir / "project.yaml" if pdir.is_dir() else pdir
    pdir = yaml.parent
    build = pdir / "build"

    if not args.upload_only:
        project = atech.Project.load(yaml)
        for sub in ("lib", "src"):
            shutil.rmtree(build / sub, ignore_errors=True)
        project.generate(build)
        notes = [] if args.stock_usb else harden(build)
        print("hardened: " + (", ".join(notes) if notes else "none (stock SDK build)"), flush=True)
        res = run_build(build)
        if not res.success:
            print(tail(res.stdout))
            print(tail(res.stderr, 1500), file=sys.stderr)
            sys.exit(res.returncode or 1)
        print(f"firmware: {res.firmware_path}", flush=True)
        if args.no_upload:
            return

    if not (build / "platformio.ini").exists():
        sys.exit(f"build_flash: nothing built in {build}; run without --upload-only first")
    up = run_upload(build, port=args.port)
    ok = getattr(up, "success", None)
    if ok is None:
        ok = up.returncode == 0
    if not ok:
        print(tail(getattr(up, "stdout", "")))
        print(tail(getattr(up, "stderr", ""), 1500), file=sys.stderr)
        sys.exit(up.returncode or 1)
    print(f"uploaded: {up.port or args.port}", flush=True)


if __name__ == "__main__":
    main()
