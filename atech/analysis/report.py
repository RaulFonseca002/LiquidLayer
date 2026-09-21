"""report.py — self-contained HTML report of the presence evaluation (timelines + scores).

    ~/.atech/venv/bin/python analysis/report.py --out /tmp/presence_report.html

Runs evaluate.evaluate() on every session in the manifest, downsamples the per-frame features to
one value per second, and draws, per protocol group: the ground-truth band, jitter and wander on a
log scale, the router frame layout, and the state of every candidate detector. Inline SVG, no
libraries; fonts from Google Fonts (IBM Plex).
"""
from __future__ import annotations

import argparse
import html
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dataset import LABEL_NAMES, load_manifest, load_session  # noqa: E402
import features as fe  # noqa: E402
import candidates as cd  # noqa: E402
import evaluate as ev  # noqa: E402

CANDS = ["A_schmitt", "B_motion_var1", "B_motion_jitter", "C_dynamic", "D_zscore", "E_logreg", "F_adaptive"]
CAND_TITLE = {"A_schmitt": "A  wander/jitter Schmitt (current firmware)", "B_motion_var1": "B  motion (1 s variance) + 60 s hold",
              "B_motion_jitter": "B' motion (jitter) + 60 s hold", "C_dynamic": "C  dynamic baseline + gate + hold",
              "D_zscore": "D  per-subcarrier z-count + hold", "E_logreg": "E  logistic regression + hold",
              "F_adaptive": "F  adaptive baseline ratio + hold"}
LABEL_COLOR = {"out": "var(--out)", "still": "var(--still)", "walk": "var(--walk)", "leave": "var(--trans)", "enter": "var(--trans)"}


def per_second(t, x, t0, t1, fn=np.nanmedian):
    secs = np.arange(int(np.floor(t0)), int(np.ceil(t1)))
    idx = np.searchsorted(t, secs)
    idx2 = np.searchsorted(t, secs + 1)
    out = np.full(len(secs), np.nan)
    for i in range(len(secs)):
        seg = x[idx[i]:idx2[i]]
        if len(seg):
            with np.errstate(all="ignore"):
                v = fn(seg.astype(float))
            out[i] = v
    return secs, out


def svg_group(gi, g, t, F, pres_by, layout, W=860):
    g0 = min(w["t0"] for w in g) - 10
    g1 = max(w["t1"] for w in g) + 10
    dur = g1 - g0
    x = lambda tt: 60 + (tt - g0) / dur * (W - 80)
    rows = []
    y = 0
    # ground truth band
    rows.append(f'<text x="0" y="{y+13}" class="lbl">truth</text>')
    for w in g:
        c = LABEL_COLOR.get(w["label"], "var(--muted)")
        rows.append(f'<rect x="{x(w["t0"]):.1f}" y="{y+2}" width="{max(1.0, x(w["t1"]) - x(w["t0"])):.1f}" height="14" fill="{c}" rx="2"/>')
        if w["t1"] - w["t0"] > 25:
            rows.append(f'<text x="{x(w["t0"]) + 4:.1f}" y="{y+13}" class="band">{w["label"]}</text>')
    y += 22
    # layout strip
    secs, lay = per_second(t, layout.astype(float), g0, g1, fn=lambda s: np.bincount(s.astype(int)).argmax())
    rows.append(f'<text x="0" y="{y+11}" class="lbl">frame</text>')
    for s, l in zip(secs, lay):
        if np.isnan(l):
            continue
        c = "var(--lay3)" if l == 3 else ("var(--lay2)" if l == 2 else "var(--muted)")
        rows.append(f'<rect x="{x(s):.1f}" y="{y+2}" width="{(W-80)/dur + 0.2:.2f}" height="10" fill="{c}"/>')
    y += 18
    # log-scale traces
    def trace(name, arr, color, lo=1e-3, hi=2.0):
        nonlocal y
        H = 64
        secs, v = per_second(t, arr, g0, g1)
        rows.append(f'<text x="0" y="{y+12}" class="lbl">{name}</text>')
        rows.append(f'<rect x="60" y="{y}" width="{W-80}" height="{H}" class="plot"/>')
        for tick, txt in ((1e-3, "0.001"), (1e-2, "0.01"), (1e-1, "0.1"), (1.0, "1")):
            yy = y + H - (np.log10(tick) - np.log10(lo)) / (np.log10(hi) - np.log10(lo)) * H
            rows.append(f'<line x1="60" x2="{W-20}" y1="{yy:.1f}" y2="{yy:.1f}" class="grid"/>')
            rows.append(f'<text x="{W-18}" y="{yy+3:.1f}" class="tick">{txt}</text>')
        pts = []
        for s, val in zip(secs, v):
            if np.isnan(val):
                if pts:
                    rows.append(f'<polyline points="{" ".join(pts)}" class="line" style="stroke:{color}"/>'); pts = []
                continue
            val = min(max(val, lo), hi)
            yy = y + H - (np.log10(val) - np.log10(lo)) / (np.log10(hi) - np.log10(lo)) * H
            pts.append(f"{x(s):.1f},{yy:.1f}")
        if pts:
            rows.append(f'<polyline points="{" ".join(pts)}" class="line" style="stroke:{color}"/>')
        y += H + 8
    trace("jitter", F["jitter"], "var(--jit)")
    trace("wander", F["wander"], "var(--wan)")
    # candidate states
    for name in CANDS:
        if name not in pres_by:
            continue
        secs, v = per_second(t, pres_by[name].astype(float), g0, g1, fn=np.mean)
        rows.append(f'<text x="0" y="{y+10}" class="lbl">{name.split("_")[0]}</text>')
        rows.append(f'<rect x="60" y="{y}" width="{W-80}" height="12" class="plot"/>')
        for s, val in zip(secs, v):
            if not np.isnan(val) and val >= 0.5:
                rows.append(f'<rect x="{x(s):.1f}" y="{y+1}" width="{(W-80)/dur + 0.2:.2f}" height="10" fill="var(--present)"/>')
        y += 15
    # time axis
    y += 4
    for k in range(0, int(dur) + 1, 60):
        rows.append(f'<line x1="{x(g0+k):.1f}" x2="{x(g0+k):.1f}" y1="{y}" y2="{y+4}" class="grid"/>')
        rows.append(f'<text x="{x(g0+k):.1f}" y="{y+14}" class="tick" text-anchor="middle">{k//60} min</text>')
    y += 20
    return f'<svg viewBox="0 0 {W} {y}" width="100%" role="img" aria-label="group {gi} timelines">{"".join(rows)}</svg>'


def build(man, out_path):
    sections = []
    tables = []
    for e in man["sessions"]:
        ses = load_session(e)
        results, _ = ev.evaluate(ses, verbose=False)
        groups = ev.groups_of(ses)
        t = ses.t
        # recompute per-group outputs for drawing (same procedure as evaluate)
        Fg = [fe.compute(ses, ev.group_ref(t, g)) for g in groups]
        for gi, g in enumerate(groups):
            train = [(Fg[j], groups[j]) for j in range(len(groups)) if j != gi]
            Fs = [F for F, _ in train]
            refs = [ev.window_mask(t, gg, {"out"}) for _, gg in train]
            poss = [ev.window_mask(t, gg, {"still", "walk"}) for _, gg in train]
            F = Fg[gi]
            pres = {
                "A_schmitt": cd.run_schmitt(F, cd.fit_schmitt(Fs, refs)),
                "B_motion_var1": cd.run_motion_hold(F, cd.fit_motion_hold(Fs, refs, feature="var1")),
                "B_motion_jitter": cd.run_motion_hold(F, cd.fit_motion_hold(Fs, refs, feature="jitter_s")),
                "D_zscore": cd.run_zscore(F, cd.fit_zscore(Fs, refs)),
                "E_logreg": cd.run_logreg(F, cd.fit_logreg(Fs, refs, poss)),
                "F_adaptive": cd.run_adaptive(F, cd.fit_adaptive(Fs, refs)),
            }
            g0 = min(w["t0"] for w in g); g1 = max(w["t1"] for w in g)
            sel = (t >= g0 + ev.GUARD_S) & (t < g1)
            Fsub = {k: (v[sel] if isinstance(v, np.ndarray) and len(v) == len(t) else v) for k, v in F.items()}
            pc, _ = cd.run_dynamic(Fsub, cd.fit_dynamic(Fs, refs), amp=ses.amp[sel], layout=ses.layout[sel])
            presC = np.zeros(len(t), dtype=bool); presC[sel] = pc
            pres["C_dynamic"] = presC
            import datetime
            start = datetime.datetime.fromtimestamp(ses.t_us[0] / 1e6 + g0).strftime("%H:%M")
            labels = " → ".join(w["label"] for w in g if w["label"] in ("out", "still", "walk"))
            rows = "".join(
                f'<tr><td>{html.escape(CAND_TITLE[n])}</td><td>{r["TPR"]*100:.0f}</td><td>{(r["TPR_still"]*100 if not np.isnan(r["TPR_still"]) else float("nan")):.0f}</td>'
                f'<td>{r["FPR"]*100:.0f}</td><td>{r["FA"]}</td><td>{r["latency_s"]:.0f}</td><td>{r["release_s"]:.0f}</td></tr>'
                for n in CANDS for r in [results[n][gi]]
            ).replace("nan", "–")
            sections.append(f'''
<section class="group">
  <header><h2>Protocol {gi + 1} <span class="when">{start}</span></h2><p class="seq">{labels}</p></header>
  {svg_group(gi, g, t, F, pres, ses.layout)}
  <div class="tablewrap"><table>
    <thead><tr><th>detector</th><th>present while occupied %</th><th>while still %</th><th>present while out %</th><th>false alarms</th><th>latency s</th><th>release s</th></tr></thead>
    <tbody>{rows}</tbody></table></div>
</section>''')
        pooled_rows = "".join(
            f'<tr><td>{html.escape(CAND_TITLE[n])}</td><td>{p["TPR"]*100:.0f}</td><td>{p["TPR_still"]*100:.0f}</td><td>{p["FPR"]*100:.0f}</td><td>{p["FA_per_h"]:.1f}</td><td>{p["latency_s"]:.0f}</td><td>{p["release_s"]:.0f}</td></tr>'
            for n in CANDS for p in [ev.pooled(results[n])]
        )
        out_s = sum(r["out_s"] for r in results["A_schmitt"]); occ_s = sum(r["occ_s"] for r in results["A_schmitt"])
        tables.append((pooled_rows, out_s, occ_s))
    pooled_rows, out_s, occ_s = tables[0]
    page = f'''<title>Room Presence Evaluation</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Sans+Condensed:wght@500;600&family=IBM+Plex+Sans:wght@400;500&family=IBM+Plex+Mono:wght@400;500&display=swap">
<style>
:root {{ --bg:#f3f5f7; --paper:#ffffff; --ink:#1b2430; --muted:#6b7784; --line:#d5dbe2; --accent:#1f6f8b;
  --out:#5b7a99; --still:#c2731f; --walk:#8b3a62; --trans:#b8c0ca; --present:#1f8b5f; --jit:#1f6f8b; --wan:#c2731f; --lay3:#9fb3c8; --lay2:#e3c58f; }}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{ --bg:#101418; --paper:#171c22; --ink:#e6ebf0; --muted:#98a4b1; --line:#2b333c; --accent:#5fb3d1;
  --out:#7a9bc0; --still:#e0995a; --walk:#c46a97; --trans:#3a434d; --present:#3fb37f; --jit:#5fb3d1; --wan:#e0995a; --lay3:#3f5468; --lay2:#7a6a3f; }} }}
:root[data-theme="dark"] {{ --bg:#101418; --paper:#171c22; --ink:#e6ebf0; --muted:#98a4b1; --line:#2b333c; --accent:#5fb3d1;
  --out:#7a9bc0; --still:#e0995a; --walk:#c46a97; --trans:#3a434d; --present:#3fb37f; --jit:#5fb3d1; --wan:#e0995a; --lay3:#3f5468; --lay2:#7a6a3f; }}
body {{ background: var(--bg); color: var(--ink); font-family: "IBM Plex Sans", system-ui, sans-serif; font-size: 15px; line-height: 1.5; margin: 0; }}
main {{ max-width: 920px; margin: 0 auto; padding: 32px 20px 64px; }}
h1, h2 {{ font-family: "IBM Plex Sans Condensed", "IBM Plex Sans", sans-serif; text-wrap: balance; margin: 0; }}
h1 {{ font-size: 30px; font-weight: 600; }} h2 {{ font-size: 20px; font-weight: 600; }}
.eyebrow {{ font-family: "IBM Plex Mono", monospace; font-size: 12px; letter-spacing: .08em; text-transform: uppercase; color: var(--accent); }}
p {{ max-width: 68ch; }} .lede {{ color: var(--muted); }}
.legend {{ display: flex; flex-wrap: wrap; gap: 14px 22px; font-size: 13px; color: var(--muted); margin: 18px 0 8px; }}
.legend span::before {{ content: ""; display: inline-block; width: 12px; height: 12px; border-radius: 2px; margin-right: 6px; vertical-align: -1px; background: var(--c); }}
section.group {{ background: var(--paper); border: 1px solid var(--line); border-radius: 6px; padding: 18px 20px; margin-top: 22px; }}
section.group header {{ display: flex; align-items: baseline; gap: 14px; flex-wrap: wrap; margin-bottom: 10px; }}
.when {{ font-family: "IBM Plex Mono", monospace; font-size: 13px; color: var(--muted); font-weight: 400; }}
.seq {{ margin: 0; color: var(--muted); font-size: 13px; }}
svg {{ display: block; }} .lbl {{ font: 500 11px "IBM Plex Mono", monospace; fill: var(--muted); }}
.band {{ font: 500 10px "IBM Plex Mono", monospace; fill: #fff; }} .tick {{ font: 400 10px "IBM Plex Mono", monospace; fill: var(--muted); }}
.plot {{ fill: none; stroke: var(--line); }} .grid {{ stroke: var(--line); stroke-width: 1; }} .line {{ fill: none; stroke-width: 1.4; }}
.tablewrap {{ overflow-x: auto; margin-top: 12px; }}
table {{ border-collapse: collapse; width: 100%; font-size: 13px; font-variant-numeric: tabular-nums; }}
th, td {{ text-align: right; padding: 6px 10px; border-bottom: 1px solid var(--line); white-space: nowrap; }}
th:first-child, td:first-child {{ text-align: left; }} th {{ color: var(--muted); font-weight: 500; }}
.summary th, .summary td {{ padding: 8px 10px; }}
.findings li {{ margin: 6px 0; }} .findings {{ padding-left: 20px; max-width: 72ch; }}
</style>
<main>
  <div class="eyebrow">Liquid node · WiFi CSI · 2026-09-06</div>
  <h1>Room Presence Evaluation</h1>
  <p class="lede">Seven detectors replayed on today's button-labelled recordings, leave-one-protocol-out. Each protocol's empty-room template comes from the first half of its own out window; thresholds come from the other protocols. Scores are read per second inside the certain windows only, transitions excluded.</p>
  <div class="legend"><span style="--c:var(--out)">out of the room</span><span style="--c:var(--still)">seated still</span><span style="--c:var(--walk)">walking</span><span style="--c:var(--trans)">leaving / entering</span><span style="--c:var(--lay3)">384-byte frames</span><span style="--c:var(--lay2)">256-byte frames</span><span style="--c:var(--present)">detector says present</span></div>
  <div class="tablewrap"><table class="summary">
    <thead><tr><th>detector (pooled over held-out protocols)</th><th>present while occupied %</th><th>while still %</th><th>present while out %</th><th>false alarms / h</th><th>latency s</th><th>release s</th></tr></thead>
    <tbody>{pooled_rows}</tbody></table></div>
  <p class="lede">Scored: {out_s} s out, {occ_s} s occupied. Release time is the hold length by design.</p>
  <ul class="findings">
    <li><strong>Protocols 1 and 2 separate cleanly.</strong> A seated person raises wander to 0.3–1.5 against an empty floor of 0.003–0.02; the adaptive-ratio detector reads both fully with no false alarms.</li>
    <li><strong>Protocol 3 is a router problem, not a room problem.</strong> Right after the 12:57 reflash the access point's transmit mode flapped every ~40 s, sending empty-room jitter from 0.15 to 1.6 and back. Every motion-based detector reads that as movement. The firmware now logs the per-frame rate and STBC flags so the next recordings can key the baseline on the router's mode.</li>
    <li><strong>Frame layout matters.</strong> 256-byte and 384-byte frames describe different channels; where the empty window had only one layout, frames of the other carry no template and are treated as unknown, not as evidence.</li>
    <li><strong>Absolute thresholds do not transfer</strong> across router modes; ratios to a tracked baseline do. A pure motion detector misses a seated person (7–35 % while still); the wander term is what carries stillness.</li>
  </ul>
  {"".join(sections)}
</main>'''
    Path(out_path).write_text(page)
    return out_path


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", default=None)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    print(build(load_manifest(a.manifest), a.out))
