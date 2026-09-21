#!/usr/bin/env python
"""simulate.py — run an Atech project's firmware on this computer before flashing.

  simulate.py <project-dir> --scenario "100 press btn; 400 release btn; 700 press btn; 1000 release btn"
              [--bounce 3] [--tick-us 1000] [--duration-ms N] [--expect "led.setAll=1,led.clear=1"]
              [--mock <module-id>] [--native <module-id>] [--max-trace 400] [--quiet]

What it does
  1. generates the exact PlatformIO tree `atech build` would (same main.cpp),
  2. compiles it with g++ against a fake Arduino core (scripts/sim/),
     - drivers that only need Arduino GPIO are compiled for real (button, motors, pir...),
     - drivers that need vendor libraries are replaced by auto-generated mocks that
       record every call and return scenario-provided values,
  3. runs setup() + loop() on a virtual clock, applying the scenario inputs,
  4. prints a trace: serial lines, GPIO/PWM changes, calls into mocked modules.

Scenario grammar (one command per `;` or newline, time in ms first)
  <ms> press <inst>      button-style input goes active (pin LOW for native active-low drivers,
                         or `ret <inst>.isPressed 1` for mocked drivers)
  <ms> release <inst>    input goes inactive
  <ms> tap <inst>        press, then release 80 ms later
  <ms> high|low <inst>   set the module's signal pin directly (1 / 0)
  <ms> pin <gpio> <0|1>  set any GPIO input
  <ms> analog <gpio> <v> set an analogRead value
  <ms> ret <inst.method> <number>   what a mocked method returns from now on
  --bounce N             add N extra contact bounces (1 ms apart) to every press/release

Exit codes: 0 ok, 1 compile error (fix code:), 3 --expect mismatch, 2 usage.
Run through atech-env.sh python. Needs g++ (any C++17 compiler named g++ or c++).
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

try:
    import atech
except ImportError:  # pragma: no cover
    sys.exit("atech SDK not importable; run via atech-env.sh python simulate.py")

sys.path.insert(0, str(Path(__file__).resolve().parent))
from atech_lint import format_lint, lint_project  # noqa: E402

SIM_DIR = Path(__file__).resolve().parent / "sim"

STD_HEADERS = {
    "stdint.h", "stddef.h", "string.h", "math.h", "limits.h", "stdlib.h", "stdio.h", "stdbool.h",
    "ctype.h", "inttypes.h", "float.h", "assert.h", "errno.h", "time.h",
}
ARITH = {
    "bool", "int", "unsigned", "long", "short", "char", "float", "double", "size_t", "ssize_t",
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "unsigned int", "unsigned long", "unsigned char", "unsigned short", "long long", "unsigned long long",
    "signed char", "byte", "boolean", "word",
}


# ----------------------------------------------------------------------------- helpers
def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def includes_of(paths: list[Path]) -> set[str]:
    inc: set[str] = set()
    for p in paths:
        for m in re.finditer(r'#include\s*[<"]([^>"]+)[>"]', p.read_text(errors="replace")):
            inc.add(m.group(1))
    return inc


def can_compile_natively(module_dir: Path) -> tuple[bool, list[str]]:
    files = sorted(module_dir.glob("*.h")) + sorted(module_dir.glob("*.cpp"))
    local = {f.name for f in files}
    foreign: list[str] = []
    for inc in includes_of(files):
        base = inc.split("/")[-1]
        if inc == "Arduino.h" or base in local or inc in STD_HEADERS:
            continue
        if "." not in base or (base.startswith("c") and not base.endswith(".h")):  # <vector>, <cmath>
            continue
        foreign.append(inc)
    return (not foreign, foreign)


def extract_class(text: str, class_name: str) -> tuple[str, str] | None:
    m = re.search(r"\bclass\s+" + re.escape(class_name) + r"\b[^{;]*\{", text)
    if not m:
        return None
    i, depth = m.end(), 1
    start = i
    while i < len(text) and depth:
        depth += (text[i] == "{") - (text[i] == "}")
        i += 1
    body = text[start:i - 1]
    rest = text[:m.start()] + text[i:]
    rest = re.sub(r"^\s*;", "", rest, count=1, flags=re.M)
    return body, rest


def split_members(body: str) -> list[tuple[str, str]]:
    members: list[tuple[str, str]] = []
    access, cur, depth, i = "private", "", 0, 0
    while i < len(body):
        c = body[i]
        if depth == 0 and not cur.strip():
            m = re.match(r"\s*(public|private|protected)\s*:", body[i:])
            if m:
                access = m.group(1)
                i += m.end()
                continue
        if c == "{":
            depth += 1
            cur += c
        elif c == "}":
            depth -= 1
            cur += c
            if depth == 0:
                j = i + 1
                while j < len(body) and body[j].isspace():
                    j += 1
                if j < len(body) and body[j] == ";":
                    cur += ";"
                    i = j
                members.append((access, cur.strip()))
                cur = ""
        elif c == ";" and depth == 0:
            members.append((access, cur.strip()))
            cur = ""
        else:
            cur += c
        i += 1
    if cur.strip():
        members.append((access, cur.strip()))
    return members


def split_params(params: str) -> list[str]:
    out, depth, cur = [], 0, ""
    for ch in params:
        if ch in "(<[":
            depth += 1
        elif ch in ")>]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [p.strip() for p in out if p.strip() and p.strip() != "void"]


def parse_param(p: str, idx: int) -> tuple[str, str, str]:
    """-> (type, name, default) where default is '' or ' = expr'."""
    default = ""
    dm = re.search(r"=(.*)$", p, flags=re.S)
    if dm:
        default = " = " + dm.group(1).strip()
        p = p[:dm.start()]
    p = p.strip()
    m = re.match(r"^(.*?[\s\*&])([A-Za-z_]\w*)\s*(\[\s*\w*\s*\])?\s*$", p)
    if m and m.group(1).strip():
        return (m.group(1).strip() + (m.group(3) or ""), m.group(2), default)
    return (p, f"a{idx}", default)


def return_stmt(rt: str, method: str, static: bool, class_name: str = "") -> str:
    rt_clean = re.sub(r"\b(const|volatile|inline|virtual|static)\b", "", rt).strip()
    self_ = "nullptr" if static else "this"
    if rt_clean in ("", "void"):
        return ""
    if rt_clean == "bool":
        return f"return sim::ret({self_}, \"{method}\", 0) != 0;"
    if rt_clean in ARITH:
        return f"return ({rt_clean})sim::ret({self_}, \"{method}\", 0);"
    if "String" in rt_clean:
        return "return String();"
    if rt_clean.endswith("*"):
        return "return nullptr;"
    if rt_clean.endswith("&"):
        target = rt_clean[:-1].strip()
        # `Foo& method()` on class Foo chains -> *this; any other referenced type
        # (e.g. a nested `const Snapshot&`) gets a static default instance.
        if not static and target.split("::")[-1] == class_name:
            return "return *this;"
        return f"static {target} _r{{}}; return _r;"
    return "return {};"


FAKE_HEADERS = {"Wire.h", "SPI.h", "freertos/FreeRTOS.h", "freertos/task.h", "freertos/queue.h", "esp_system.h", "Preferences.h"}


def generate_mock(header_text: str, class_name: str, module_id: str, foreign: list[str], siblings: set[str] = frozenset()) -> str:
    text = strip_comments(header_text)
    found = extract_class(text, class_name)
    if not found:
        raise RuntimeError(f"class {class_name} not found in driver header of {module_id}")
    body, rest = found
    lines = [
        "#pragma once",
        "#include <Arduino.h>",
        f"// Auto-generated mock of {class_name} ({module_id}); the real driver needs {', '.join(foreign) or 'hardware'}.",
        "// Every call is recorded; non-void methods return scenario `ret` values (default 0).",
    ]
    # keep the includes main.cpp relies on transitively: sibling helper headers and the buses we fake
    added: set[str] = set()
    for m in re.finditer(r'#include\s*([<"])([^>"]+)[>"]', header_text):
        inc = m.group(2)
        base = inc.split("/")[-1]
        if base in siblings:                       # helper header copied next to the mock
            lines.append(f'#include "{base}"')
            added.add(base)
        elif inc in FAKE_HEADERS:                  # a bus/RTOS header we provide in scripts/sim
            lines.append(f"#include <{inc}>")
            added.add(inc)
        # anything else (vendor libraries, ESP-IDF drivers) is exactly what the mock removes
    for bus in ("Wire.h", "SPI.h"):  # main.cpp may pass `Wire`/`SPI` to the constructor even if only the .cpp included it
        if bus in foreign and bus not in added:
            lines.append(f"#include <{bus}>")
    # top-level helper types / defines the API may reference
    for m in re.finditer(r"^\s*#define\s+(?!.*_H\b)\w+.*$", header_text, flags=re.M):
        lines.append(m.group(0).strip())
    for _, decl in split_members(rest):
        if re.match(r"^(enum|struct|union|typedef|using|constexpr|static\s+const)\b", decl) and not re.match(r"^struct\s+\w+\s*;$", decl):
            lines.append(decl if decl.endswith(";") else decl + ";")
    lines += [f"class {class_name} {{", "public:"]
    # types the mock may legitimately name in signatures
    known = set(ARITH) | {"String", "char", "void", "std::string", "auto", class_name}
    known |= set(re.findall(r"\b(?:enum|struct|class|union)\s+(?:class\s+)?(\w+)", text))
    known |= set(re.findall(r"\btypedef\b[^;]*?\b(\w+)\s*;", text))
    known |= set(re.findall(r"\busing\s+(\w+)\s*=", text))
    verbatim, methods = [], []
    for access, decl in split_members(body):
        if access != "public" or not decl:
            continue
        if re.match(r"^(enum|struct|union|typedef|using)\b", decl):
            verbatim.append(decl if decl.endswith(";") else decl + ";")
            continue
        if re.match(r"^(static\s+)?(const\s+|constexpr\s+)+", decl) and "=" in decl and decl.find("=") < (decl.find("(") if "(" in decl else len(decl)):
            verbatim.append(decl if decl.endswith(";") else decl + ";")
            continue
        if "(" not in decl:
            verbatim.append(decl if decl.endswith(";") else decl + ";")  # public data member
            continue
        d = re.sub(r"\{.*\}\s*;?$", "", decl, flags=re.S).strip()
        d = re.sub(r"\b(virtual|explicit|inline|override|final)\b", "", d)
        d = re.sub(r"=\s*(0|default|delete)\s*;?$", "", d).strip().rstrip(";").strip()
        paren = d.find("(")
        head = d[:paren].strip()
        depth, j = 0, paren
        while j < len(d):
            depth += (d[j] == "(") - (d[j] == ")")
            j += 1
            if depth == 0:
                break
        params_txt, tail = d[paren + 1:j - 1], d[j:].strip()
        name_m = re.search(r"(~?\w+|operator\S*)\s*$", head)
        if not name_m:
            continue
        name = name_m.group(1)
        if name.startswith("~") or name == class_name or name.startswith("operator"):
            continue
        rt = head[:name_m.start()].strip()
        tpl_params: list[str] = []
        tm = re.match(r"^\s*template\s*<(.*?)>\s*", rt, flags=re.S)
        if tm:
            tpl_params = [p.strip().split()[-1] for p in split_params(tm.group(1)) if p.strip()]
            rt = rt[tm.end():].strip()
        static = bool(re.search(r"\bstatic\b", rt))
        rt = re.sub(r"\bstatic\b", "", rt).strip()
        params = [parse_param(p, i) for i, p in enumerate(split_params(params_txt))]
        # a parameter whose type we cannot name on the host (vendor library type) becomes a template parameter
        local_known = known | set(tpl_params)
        fixed = []
        for i, (t, n, dflt) in enumerate(params):
            base = re.sub(r"\b(const|volatile|unsigned|signed|struct|enum)\b|[\*&]|\[.*\]", " ", t).strip()
            base_ok = not base or all(tok in local_known for tok in base.split())
            if base_ok:
                fixed.append((t, n, dflt))
            else:
                tp = f"TArg{i}"
                tpl_params.append(tp)
                fixed.append((f"{tp}&&", n, ""))  # template params cannot carry the vendor default
        params = fixed
        sig = ", ".join((f"{t} {n}" if not t.endswith("]") else f"{t[:t.index('[')]} {n}{t[t.index('['):]}") + d for t, n, d in params)
        args = ", ".join(n for _, n, _ in params)
        self_ = "nullptr" if static else "this"
        const = " const" if re.match(r"^const\b", tail) else ""
        rs = return_stmt(rt, name, static, class_name)
        tpl = f"template <{', '.join('class ' + p for p in tpl_params)}> " if tpl_params else ""
        methods.append(f"    {tpl}{'static ' if static else ''}{rt} {name}({sig}){const} {{ sim::call({self_}, \"{name}\"{', ' + args if args else ''}); {rs} }}")
    lines += ["    " + v for v in verbatim]
    lines.append(f"    template <class... A> {class_name}(A&&...) {{}}")
    lines += methods
    lines += ["};", ""]
    return "\n".join(lines)


# ----------------------------------------------------------------------------- scenario
def signal_pin(project, inst: str) -> int | None:
    board = project.board_spec
    for pm in project.modules:
        if pm.instance == inst:
            pid = pm.ports[0]
            for p in board.ports:
                if p.id == pid and p.pins:
                    return p.pins[0].gpio
    return None


def module_of(project, inst: str):
    for pm in project.modules:
        if pm.instance == inst:
            return pm
    return None


def compile_scenario(project, text: str, bounce: int, native: set[str]) -> tuple[list[str], int]:
    lines: list[str] = []
    last = 0

    def pin_event(t: int, gpio: int, val: int) -> None:
        nonlocal last
        for k in range(bounce):
            lines.append(f"{t + 2 * k} pin {gpio} {val}")
            lines.append(f"{t + 2 * k + 1} pin {gpio} {1 - val}")
        lines.append(f"{t + 2 * bounce} pin {gpio} {val}")
        last = max(last, t + 2 * bounce)

    def input_event(t: int, inst: str, active: bool) -> None:
        nonlocal last
        pm = module_of(project, inst)
        if pm is None:
            sys.exit(f"scenario: unknown instance '{inst}' (have: {[m.instance for m in project.modules]})")
        if pm.module_id in native:
            gpio = signal_pin(project, inst)
            if gpio is None:
                sys.exit(f"scenario: {inst} has no signal pin")
            pin_event(t, gpio, 0 if active else 1)  # active-low inputs (button)
        else:
            lines.append(f"{t} ret {inst}.isPressed {1 if active else 0}")
            lines.append(f"{t} ret {inst}.getState {1 if active else 0}")
            last = max(last, t)

    for raw in re.split(r"[;\n]", text or ""):
        cmd = raw.strip()
        if not cmd or cmd.startswith("#"):
            continue
        parts = cmd.split()
        try:
            t = int(parts[0].rstrip(":"))
        except ValueError:
            sys.exit(f"scenario: expected '<ms> <command>', got: {cmd}")
        verb = parts[1].lower() if len(parts) > 1 else ""
        args = parts[2:]
        if verb in ("press", "down") and len(args) == 1:
            input_event(t, args[0], True)
        elif verb in ("release", "up") and len(args) == 1:
            input_event(t, args[0], False)
        elif verb == "tap" and len(args) == 1:
            input_event(t, args[0], True)
            input_event(t + 80, args[0], False)
        elif verb in ("high", "low") and len(args) == 1:
            gpio = signal_pin(project, args[0])
            if gpio is None:
                sys.exit(f"scenario: {args[0]} has no signal pin")
            pin_event(t, gpio, 1 if verb == "high" else 0)
        elif verb == "pin" and len(args) == 2:
            pin_event(t, int(args[0]), int(args[1]))
        elif verb == "analog" and len(args) == 2:
            lines.append(f"{t} analog {args[0]} {args[1]}")
            last = max(last, t)
        elif verb == "ret" and len(args) == 2:
            lines.append(f"{t} ret {args[0]} {args[1]}")
            last = max(last, t)
        else:
            sys.exit(f"scenario: cannot parse: {cmd}")
    return lines, last


# ----------------------------------------------------------------------------- main
def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("project")
    ap.add_argument("--scenario", default="", help="see grammar above")
    ap.add_argument("--scenario-file", default=None)
    ap.add_argument("--bounce", type=int, default=0)
    ap.add_argument("--tick-us", type=int, default=1000, help="virtual time per loop() iteration")
    ap.add_argument("--duration-ms", type=int, default=None)
    ap.add_argument("--expect", default="", help="comma list of name.method=count assertions on mocked calls")
    ap.add_argument("--mock", action="append", default=[], help="force-mock this module id")
    ap.add_argument("--native", action="append", default=[], help="force native compile of this module id")
    ap.add_argument("--max-trace", type=int, default=400)
    ap.add_argument("--quiet", action="store_true", help="only summary and warnings, no trace")
    args = ap.parse_args()

    pdir = Path(args.project).expanduser().resolve()
    try:
        project = atech.Project.load(pdir / "project.yaml" if pdir.is_dir() else pdir)
    except Exception as exc:  # PlacementError, unknown module, yaml errors
        print(f"# atech simulate — cannot load project: {type(exc).__name__}")
        print(str(exc).strip())
        print("fix project.yaml (see catalog.py --board <id> for sizes, reserved ports and adjacent pairs)")
        sys.exit(1)
    simdir = (pdir if pdir.is_dir() else pdir.parent) / "sim"
    gen, mocks = simdir / "gen", simdir / "mocks"
    shutil.rmtree(simdir, ignore_errors=True)
    mocks.mkdir(parents=True)

    print(f"# atech simulate — {project.name} ({project.board_spec.id})")
    issues = project.validate()
    if issues:
        for i in issues:
            print(f"placement PROBLEM: {i}")
        sys.exit(1)

    lint = lint_project(project)
    print("lint:")
    print(format_lint(lint))

    project.generate(out_dir=gen)
    main_cpp = gen / "src" / "main.cpp"

    include_dirs = [SIM_DIR, mocks]
    native_cpps: list[Path] = []
    native_ids: set[str] = set()
    labels = []
    for pm in project.modules:
        spec = project.module_spec(pm.module_id)
        mdir = gen / "lib" / f"atech_{pm.module_id}"
        if not mdir.exists():
            cands = [d for d in (gen / "lib").iterdir() if (d / spec.driver.header).exists()]
            mdir = cands[0] if cands else mdir
        ok, foreign = can_compile_natively(mdir)
        if pm.module_id in args.mock:
            ok = False
        if pm.module_id in args.native:
            ok = True
        if ok:
            native_ids.add(pm.module_id)
            include_dirs.append(mdir)
            native_cpps += sorted(mdir.glob("*.cpp"))
            labels.append(f"{pm.instance}={pm.module_id} [real driver]")
        else:
            header = (mdir / spec.driver.header).read_text(errors="replace")
            siblings = {h.name for h in mdir.glob("*.h") if h.name != spec.driver.header}
            (mocks / spec.driver.header).write_text(generate_mock(header, spec.driver.class_name, pm.module_id, foreign, siblings))
            # helper headers the generated main.cpp may need (e.g. i2c_hardware.h -> WireI2C)
            for h in mdir.glob("*.h"):
                if h.name in siblings and not (mocks / h.name).exists():
                    shutil.copy(h, mocks / h.name)
            labels.append(f"{pm.instance}={pm.module_id} [mock; real driver needs {', '.join(foreign) or 'hardware'}]")
    print("modules: " + "; ".join(labels))

    scen_text = args.scenario
    if args.scenario_file:
        scen_text += "\n" + Path(args.scenario_file).read_text()
    scen_lines, last = compile_scenario(project, scen_text, args.bounce, native_ids)
    duration = args.duration_ms or max(500, last + 300)
    scen_file = simdir / "scenario.txt"
    scen_file.write_text("\n".join(scen_lines) + "\n")
    print(f"scenario: {len(scen_lines)} input events, bounce={args.bounce}, tick={args.tick_us}us, duration={duration}ms")

    regs = "\n".join(f"    sim::register_instance(&{pm.instance}, \"{pm.instance}\");" for pm in project.modules)
    harness = f'''// generated by simulate.py
#include "{main_cpp.as_posix()}"
int main(int argc, char** argv) {{
{regs}
    sim::load_scenario(argc > 1 ? argv[1] : "");
    unsigned long long tick_us = argc > 2 ? strtoull(argv[2], 0, 10) : 1000;
    unsigned long long duration_ms = argc > 3 ? strtoull(argv[3], 0, 10) : 1000;
    sim::S().max_trace = argc > 4 ? (size_t)strtoull(argv[4], 0, 10) : 400;
    setup();
    sim::S().in_loop = true;
    while (sim::now_ms() < duration_ms) {{ sim::apply_events(); loop(); sim::advance_us(tick_us); }}
    sim::dump();
    return 0;
}}
'''
    (simdir / "harness.cpp").write_text(harness)

    cxx = shutil.which("g++") or shutil.which("c++") or shutil.which("clang++")
    if not cxx:
        sys.exit("no C++ compiler found (need g++/c++/clang++) — install build-essential or Xcode CLT to simulate")
    binary = simdir / "firmware_sim"
    cmd = [cxx, "-std=c++17", "-O0", "-w", "-fmax-errors=8", "-DATECH_SIM=1"] + [f"-I{d}" for d in include_dirs] + \
          [str(simdir / "harness.cpp")] + [str(c) for c in native_cpps] + ["-o", str(binary)]
    t0 = time.time()
    cp = subprocess.run(cmd, capture_output=True, text=True)
    if cp.returncode != 0:
        print(f"compile: FAILED ({time.time() - t0:.1f}s). Errors (main.cpp lines refer to the generated file; your code: starts after '// ---- user loop ----'):")
        err = [l for l in cp.stderr.splitlines() if "error" in l or "main.cpp" in l or "note:" in l]
        for l in (err or cp.stderr.splitlines())[:30]:
            print("   " + l.replace(str(gen) + "/", ""))
        # show the user loop with line numbers to map errors
        src = main_cpp.read_text().splitlines()
        for n, l in enumerate(src, 1):
            if "user loop" in l or "user setup" in l:
                print(f"   --- {l.strip()} at main.cpp:{n}")
        sys.exit(1)
    print(f"compile: ok ({time.time() - t0:.1f}s)")

    run = subprocess.run([str(binary), str(scen_file), str(args.tick_us), str(duration), str(args.max_trace)],
                         capture_output=True, text=True, errors="replace", timeout=120)
    out = run.stdout.splitlines()
    trace = [l for l in out if not l.startswith("#")]
    summary = [l for l in out if l.startswith("#")]
    if not args.quiet:
        print("--- trace ---")
        for l in trace:
            print(l)
    print("--- summary ---")
    for l in summary:
        print(l)
    if run.returncode != 0:
        print(f"# firmware_sim exited with {run.returncode}: {run.stderr.strip()[:300]}")
        sys.exit(1)

    if args.expect:
        counts = {}
        for l in summary:
            m = re.match(r"# calls: (\S+)\s+x(\d+)", l)
            if m:
                counts[m.group(1)] = int(m.group(2))
        bad = []
        for item in args.expect.split(","):
            if "=" not in item:
                continue
            k, v = item.split("=", 1)
            k, v = k.strip(), int(v)
            if counts.get(k, 0) != v:
                bad.append(f"{k}: expected {v}, got {counts.get(k, 0)}")
        if bad:
            print("EXPECT FAILED: " + "; ".join(bad))
            sys.exit(3)
        print("expect: ok")


if __name__ == "__main__":
    main()
