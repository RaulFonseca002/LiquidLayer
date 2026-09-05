#!/usr/bin/env python
"""catalog.py — discovery for the agent, run through atech-env.sh python.

  catalog.py                       boards + module table
  catalog.py --board 8port         + that board's layout, reserved ports, adjacent pairs, pins
  catalog.py button neopixel       + full public C++ API (`usage`), events and actions of those modules
  catalog.py --all                 usage of every bundled module (long)

Everything printed here is the ONLY source of truth for module ids, method
names, and port rules. Never invent a module or a method that is not listed.
"""
from __future__ import annotations

import argparse
import sys

try:
    import atech
except ImportError:  # pragma: no cover
    sys.exit("atech SDK not importable; run via atech-env.sh python catalog.py")


def _attr(obj, name, default=None):
    return getattr(obj, name, default) if not isinstance(obj, dict) else obj.get(name, default)


def print_boards() -> None:
    print("## Boards")
    for b in atech.list_boards():
        ports = [p for p in _attr(b, "ports", []) if _attr(p, "id") not in set(_attr(b, "reserved", []))]
        print(f"- {_attr(b, 'id'):8} {_attr(b, 'name')}  ({len(ports)} module slots)")
    print()


def print_modules() -> None:
    print("## Modules  (id | category | interface | width | name)")
    for m in atech.list_modules():
        print(f"- {_attr(m, 'id'):20} {_attr(m, 'category'):10} {_attr(m, 'interface'):6} size={_attr(m, 'size')}  {_attr(m, 'name')}")
    print()


def print_board(board_id: str) -> None:
    b = atech.get_board(board_id)
    reserved = set(_attr(b, "reserved", []))
    print(f"## Board {_attr(b, 'id')} — {_attr(b, 'name')}")
    mc = _attr(b, "microcontroller", {}) or {}
    if mc:
        print(f"MCU: {mc.get('variant') or mc.get('type')}  flash={mc.get('flash_size_kb')}KB ram={mc.get('ram_size_kb')}KB")
    print("Layout (slot numbers; '' is a physical gap; words are fixed hardware):")
    for row in _attr(b, "layout", []):
        print("   " + " | ".join(f"{c or '·':>8}" for c in row))
    print(f"Reserved (cannot host a module): {sorted(reserved) or 'none'}")
    pairs = _attr(b, "adjacent_port_pairs", [])
    print(f"Adjacent pairs for double-width (size=2) modules: {[list(p) for p in pairs] or 'none'}")
    notes = (_attr(b, "notes") or "").strip()
    if notes:
        print("Notes: " + " ".join(notes.split()))
    print("Ports:")
    for p in _attr(b, "ports", []):
        pid = _attr(p, "id")
        flag = "  (RESERVED)" if pid in reserved else ""
        pins = ", ".join(f"{_attr(x, 'function')}=GPIO{_attr(x, 'gpio')}" for x in _attr(p, "pins", []))
        print(f"- {pid:8} slot={_attr(p, 'slot_number')} side={_attr(p, 'side')} ifaces={list(_attr(p, 'compatible_interfaces', []))} pins[{pins}]{flag}")
    print()


def print_module(module_id: str) -> None:
    try:
        m = atech.get_module(module_id)
    except Exception as exc:  # unknown id
        print(f"## {module_id}: NOT IN CATALOG ({exc})\n")
        return
    print(f"## Module {_attr(m, 'id')} — {_attr(m, 'name')}")
    print(f"category={_attr(m, 'category')} interface={_attr(m, 'interface')} size={_attr(m, 'size')}"
          + (f"  needs an adjacent port pair" if _attr(m, "size") == 2 else ""))
    desc = (_attr(m, "description") or "").strip()
    if desc:
        print("Description: " + " ".join(desc.split()))
    evs = _attr(m, "events", [])
    acts = _attr(m, "actions", [])
    if evs:
        print("Events emitted automatically by the module template:")
        for e in evs:
            unit = f" [{_attr(e, 'unit')}]" if _attr(e, "unit") else ""
            print(f"  - {_attr(e, 'key')} ({_attr(e, 'type')}, {_attr(e, 'value_type')}{unit}): {_attr(e, 'description') or ''}")
    if acts:
        print("Actions the host can send with `atech send <key> <value>`:")
        for a in acts:
            print(f"  - {_attr(a, 'key')} ({_attr(a, 'value_type')}): {_attr(a, 'description') or ''}")
    usage = (_attr(m, "usage") or "").rstrip()
    print("Public C++ API (`{{ instance }}` is the instance name you choose in project.yaml):")
    print("```cpp")
    print(usage or "// (no usage snippet published for this module)")
    print("```")
    print()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("modules", nargs="*", help="module ids to show usage for")
    ap.add_argument("--board", help="board id to detail")
    ap.add_argument("--all", action="store_true", help="show usage for every module")
    args = ap.parse_args()

    print_boards()
    print_modules()
    if args.board:
        print_board(args.board)
    ids = list(args.modules)
    if args.all:
        ids = [_attr(m, "id") for m in atech.list_modules()]
    for mid in ids:
        print_module(mid)


if __name__ == "__main__":
    main()
