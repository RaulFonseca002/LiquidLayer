"""atech_lint.py — static checks on a project's `code:` block, shared by
describe_project.py and simulate.py. Import-only; no CLI.

Catches the traps we have actually hit:
  * the module's own template consumes a one-shot flag (wasPressed & co.)
    before the user loop runs, so the user's call almost never fires;
  * delay() in loop, which stalls every module's event stream.
"""
from __future__ import annotations

import re

ONE_SHOT = re.compile(r"^(was|consume|take|pop|poll|read[A-Z]\w*Event|get\w*Event|\w+Changed)")


def _calls(code: str, instance: str) -> set[str]:
    return set(re.findall(r"\b" + re.escape(instance) + r"\s*\.\s*(\w+)\s*\(", code or ""))


def _render(template: str, instance: str) -> str:
    return re.sub(r"\{\{\s*instance\s*\}\}", instance, template or "")


def lint_project(project) -> list[tuple[str, str]]:
    """Return [(level, message)], level in {'WARNING', 'INFO'}."""
    out: list[tuple[str, str]] = []
    user_code = (getattr(project, "loop_code", "") or "") + "\n" + (getattr(project, "setup_code", "") or "")

    for pm in getattr(project, "modules", []):
        inst = pm.instance
        spec = project.module_spec(pm.module_id)
        tpl_loop = _render(getattr(spec.templates, "loop", ""), inst)
        shared = _calls(tpl_loop, inst) & _calls(project.loop_code or "", inst)
        for m in sorted(shared):
            if ONE_SHOT.match(m):
                out.append(("WARNING",
                            f"{inst}.{m}() is already called every loop by the {pm.module_id} module template, which runs "
                            f"BEFORE your code and clears the one-shot flag. Your {inst}.{m}() will only fire on contact bounce. "
                            f"Poll the state instead (e.g. isPressed()/getState()) and detect the edge yourself with a static "
                            f"variable and a millis() debounce."))
            else:
                out.append(("INFO", f"{inst}.{m}() is also called by the {pm.module_id} template each loop; fine unless it has side effects."))

    if re.search(r"\bdelay\s*\(", project.loop_code or ""):
        out.append(("WARNING", "delay() inside loop blocks every module's event stream and button polling; use a millis() timer."))
    return out


def format_lint(items: list[tuple[str, str]]) -> str:
    if not items:
        return "- ok: no known traps in code:"
    return "\n".join(f"- {lvl}: {msg}" for lvl, msg in items)
