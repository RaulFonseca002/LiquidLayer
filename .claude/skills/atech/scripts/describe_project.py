#!/usr/bin/env python
"""describe_project.py <project-dir> — assembly guide for a project.yaml.

Prints, for the human who has to plug modules into the motherboard:
  * an ASCII picture of the board with each module in its slot,
  * a module -> port table with the GPIO pins each port exposes,
  * placement validation (same checks as `atech validate`),
  * the SDK's own describe() summary.

Run through atech-env.sh python. Read-only; never modifies the project.
"""
from __future__ import annotations

import sys
from pathlib import Path

try:
    import atech
except ImportError:  # pragma: no cover
    sys.exit("atech SDK not importable; run via atech-env.sh python describe_project.py <dir>")

sys.path.insert(0, str(Path(__file__).resolve().parent))
from atech_lint import format_lint, lint_project  # noqa: E402
from atech_names import full_name, short_name  # noqa: E402


def _attr(obj, name, default=None):
    return getattr(obj, name, default) if not isinstance(obj, dict) else obj.get(name, default)


def _slot_of(port) -> str:
    """'port_3' -> '3', 3 -> '3'."""
    s = str(port)
    return s.split("_", 1)[1] if s.startswith("port_") else s


def render_layout(board, modules) -> str:
    slot_to_label: dict[str, str] = {}
    for pm in modules:
        ports = _attr(pm, "ports", []) or ([_attr(pm, "port")] if _attr(pm, "port") else [])
        mid = _attr(pm, "module_id") or _attr(pm, "id")
        for p in ports:
            slot_to_label[_slot_of(p)] = short_name(mid)
    reserved_slots = {_slot_of(r) for r in _attr(board, "reserved", [])}
    width = max([16] + [len(v) + 4 for v in slot_to_label.values()])
    lines = []
    for row in _attr(board, "layout", []):
        cells = []
        for c in row:
            if c == "":
                cells.append(" " * width)
            elif c.isdigit():
                if c in slot_to_label:
                    cells.append(f"[{c}] {slot_to_label[c]}".ljust(width))
                elif c in reserved_slots:
                    cells.append(f"[{c}] (reserved)".ljust(width))
                else:
                    cells.append(f"[{c}] empty".ljust(width))
            else:
                cells.append(f"({c})".ljust(width))
        lines.append("  " + " ".join(cells).rstrip())
    return "\n".join(lines)


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    pdir = Path(sys.argv[1]).expanduser().resolve()
    yaml_path = pdir / "project.yaml" if pdir.is_dir() else pdir
    project = atech.Project.load(yaml_path)
    board = project.board_spec

    print(f"# {project.name}  (board: {_attr(board, 'id')} — {_attr(board, 'name')})")
    print(f"project.yaml: {yaml_path}")
    print()
    print("## Board layout (USB-C side is where the cable goes)")
    print(render_layout(board, project.modules))
    print("   (labels are commercial module names; (Restart) and (USB-C) are")
    print("    fixed board hardware, not module slots)")
    print()

    print("## Assembly: module -> port")
    ports_by_id = {_attr(p, "id"): p for p in _attr(board, "ports", [])}
    if not project.modules:
        print("- no modules placed")
    for pm in project.modules:
        mid = _attr(pm, "module_id") or _attr(pm, "id")
        spec = project.module_spec(mid)
        ports = list(_attr(pm, "ports", []) or [])
        pretty = " + ".join(f"port {_slot_of(p)}" for p in ports)
        size_note = " (double-width, occupies both)" if _attr(spec, "size") == 2 else ""
        pins = []
        for p in ports:
            ps = ports_by_id.get(str(p)) or ports_by_id.get(f"port_{_slot_of(p)}")
            if ps:
                pins.append(", ".join(f"GPIO{_attr(x, 'gpio')}" for x in _attr(ps, "pins", [])))
        print(f"- {_attr(spec, 'name')} (`{mid}`, instance `{_attr(pm, 'instance')}`) -> {pretty}{size_note}"
              + (f"  pins: {' / '.join(pins)}" if pins else ""))
    print()

    issues = project.validate()
    print("## Placement check")
    if issues:
        for i in issues:
            print(f"- PROBLEM: {i}")
    else:
        print("- ok: every module sits on a valid port")
    print()

    print("## Code check (known traps)")
    print(format_lint(lint_project(project)))
    print()

    print("## SDK summary")
    print(project.describe())


if __name__ == "__main__":
    main()
