"""evaluate.py — score the candidate detectors on the labelled windows, leave-one-group-out.

Groups are contiguous protocol runs (out/still/walk of one button protocol, or a lone out run).
For every held-out group the thresholds/models are fitted on the other groups' labelled frames and
the candidate runs over the whole session; metrics are read only inside the held-out group's
certain windows (transitions excluded, ±5 s guard).

    ~/.atech/venv/bin/python analysis/evaluate.py [--manifest analysis/sessions.json] [--json out.json]

Metrics per candidate (pooled over held-out groups):
  TPR   fraction of certain occupied seconds (still + walk) reported present
  FPR   fraction of certain out seconds reported present
  FA/h  false-alarm episodes per hour of out (rising edges inside out windows)
  flick state changes per minute inside still+out windows
  lat   median seconds from the start of a still window (the press after returning) to first present
  rel   median seconds from the start of a leave window to first absent (dominated by the hold)
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dataset import LABELS, load_manifest, load_session  # noqa: E402
import features as fe  # noqa: E402
import candidates as cd  # noqa: E402

GUARD_S = 5.0


def groups_of(ses):
    """Split the manifest windows into contiguous groups: a group ends after a walk window or an
    out run that is not followed by still."""
    ws = sorted(ses.windows, key=lambda w: w["t0"])
    groups, cur = [], []
    for w in ws:
        cur.append(w)
        nxt = ws[ws.index(w) + 1] if ws.index(w) + 1 < len(ws) else None
        if w["label"] == "walk" or (w["label"] == "enter" and (nxt is None or nxt["label"] != "still")):
            groups.append(cur); cur = []
    if cur:
        groups.append(cur)
    return groups


def window_mask(t, windows, labels, guard=GUARD_S):
    m = np.zeros(len(t), dtype=bool)
    for w in windows:
        if w["label"] in labels:
            m |= (t >= w["t0"] + guard) & (t < w["t1"] - guard)
    return m


def per_second(t, x, mask):
    """Majority vote of x per whole second over masked frames -> (seconds, values)."""
    sec = np.floor(t[mask]).astype(np.int64)
    if len(sec) == 0:
        return np.array([]), np.array([])
    uniq, inv = np.unique(sec, return_inverse=True)
    votes = np.bincount(inv, weights=x[mask].astype(float))
    cnt = np.bincount(inv)
    return uniq, votes / cnt >= 0.5


def episodes(x):
    """Rising edges of a boolean series."""
    x = x.astype(np.int8)
    return int(((x[1:] - x[:-1]) > 0).sum())


def score(t, pres, windows):
    out_m = window_mask(t, windows, {"out"})
    occ_m = window_mask(t, windows, {"still", "walk"})
    still_m = window_mask(t, windows, {"still"})
    s_out, v_out = per_second(t, pres, out_m)
    s_occ, v_occ = per_second(t, pres, occ_m)
    s_st, v_st = per_second(t, pres, still_m)
    res = {"out_s": len(s_out), "occ_s": len(s_occ), "still_s": len(s_st),
           "FPR": float(v_out.mean()) if len(v_out) else np.nan,
           "TPR": float(v_occ.mean()) if len(v_occ) else np.nan,
           "TPR_still": float(v_st.mean()) if len(v_st) else np.nan,
           "FA": episodes(v_out) if len(v_out) else 0}
    # flicker: state changes per minute in still + out windows
    s_all, v_all = per_second(t, pres, out_m | still_m)
    res["flicker_per_min"] = float(((v_all[1:] != v_all[:-1]).sum()) / max(len(v_all) / 60.0, 1e-9)) if len(v_all) > 1 else np.nan
    # latency: still window start -> first present ; release: leave window start -> first absent
    lat, rel = [], []
    for w in windows:
        if w["label"] == "still":
            m = (t >= w["t0"]) & (t < w["t1"])
            idx = np.where(m & pres)[0]
            lat.append(float(t[idx[0]] - w["t0"]) if len(idx) else float(w["t1"] - w["t0"]))
        if w["label"] == "leave":
            m = (t >= w["t0"]) & (t < w["t1"] + 120.0)
            idx = np.where(m & ~pres)[0]
            rel.append(float(t[idx[0]] - w["t0"]) if len(idx) else 120.0)
    res["latency_s"] = float(np.median(lat)) if lat else np.nan
    res["release_s"] = float(np.median(rel)) if rel else np.nan
    return res


TEMPLATE_S = 20.0   # seconds of the group's own out window used as its calibration template


def group_ref(t, g, guard=GUARD_S, span=TEMPLATE_S):
    """Frames of the group's out window used as its own templates: the first half of the window
    (after the guard), so every frame layout the router used while the room was empty gets a
    template — what lazy template learning does in a deployment. FPR is then read on the second
    half only (see score_mask) to keep the estimate honest."""
    m = np.zeros(len(t), dtype=bool)
    for w in g:
        if w["label"] == "out":
            m |= (t >= w["t0"] + guard) & (t < w["t0"] + 0.5 * (w["t1"] - w["t0"]))
    return m


def second_half_out(g):
    """Out windows restricted to their second half (the template came from the first half)."""
    out = []
    for w in g:
        if w["label"] == "out":
            out.append({**w, "t0": w["t0"] + 0.5 * (w["t1"] - w["t0"])})
        else:
            out.append(w)
    return out


def evaluate(ses, verbose=True):
    """Per group: template from the group's own first TEMPLATE_S seconds of out (what a deployment
    would calibrate on), thresholds/models from the OTHER groups' labelled frames computed with
    their own templates (so scales are comparable), scoring inside the held-out group only."""
    groups = groups_of(ses)
    t = ses.t
    Fg = [fe.compute(ses, group_ref(t, g)) for g in groups]
    names = ("A_schmitt", "B_motion_var1", "B_motion_jitter", "C_dynamic", "D_zscore", "E_logreg")
    results = {k: [] for k in names}
    traces = {}
    for gi, g in enumerate(groups):
        train = [(Fg[j], groups[j]) for j in range(len(groups)) if j != gi]
        Fs = [F for F, _ in train]
        refs = [window_mask(t, gg, {"out"}) for _, gg in train]
        poss = [window_mask(t, gg, {"still", "walk"}) for _, gg in train]
        F = Fg[gi]
        fits = {
            "A_schmitt": (cd.fit_schmitt(Fs, refs), cd.run_schmitt),
            "B_motion_var1": (cd.fit_motion_hold(Fs, refs, feature="var1"), cd.run_motion_hold),
            "B_motion_jitter": (cd.fit_motion_hold(Fs, refs, feature="jitter_s"), cd.run_motion_hold),
            "D_zscore": (cd.fit_zscore(Fs, refs), cd.run_zscore),
            "E_logreg": (cd.fit_logreg(Fs, refs, poss), cd.run_logreg),
        }
        outs = {name: run(F, p) for name, (p, run) in fits.items()}
        # dynamic baseline: runs from the group's out start, bootstraps its template there
        pC = cd.fit_dynamic(Fs, refs)
        g0 = min(w["t0"] for w in g); g1 = max(w["t1"] for w in g)
        sel = (t >= g0 + GUARD_S) & (t < g1)
        Fsub = {k: (v[sel] if isinstance(v, np.ndarray) and len(v) == len(t) else v) for k, v in F.items()}
        presC_sub, wander_sub = cd.run_dynamic(Fsub, pC, amp=ses.amp[sel], layout=ses.layout[sel])
        presC = np.zeros(len(t), dtype=bool); presC[sel] = presC_sub
        wanderC = np.full(len(t), np.nan); wanderC[sel] = wander_sub
        outs["C_dynamic"] = presC
        outs["F_adaptive"] = cd.run_adaptive(F, cd.fit_adaptive(Fs, refs))
        gs = second_half_out(g)
        for name, pres in outs.items():
            r = score(t, pres, gs)
            r["group"] = gi
            results.setdefault(name, []).append(r)
        if verbose:
            print(f"group {gi}: {[w['label'] for w in g]}  template frames {group_ref(t, g).sum()}")
            for n in results:
                r = results[n][-1]
                print(f"    {n:16s} TPR {r['TPR']:.2f} still {r['TPR_still']:.2f} FPR {r['FPR']:.2f} FA {r['FA']} lat {r['latency_s']:.0f}s rel {r['release_s']:.0f}s")
        if gi == 0:
            traces = {"t": t, "label": ses.label, "F": F, "pres": outs, "wanderC": wanderC,
                      "params": {k: v[0] for k, v in fits.items()} | {"C_dynamic": pC}}
    return results, traces


def pooled(rs):
    out_s = sum(r["out_s"] for r in rs); occ_s = sum(r["occ_s"] for r in rs)
    fpr = sum(r["FPR"] * r["out_s"] for r in rs if not np.isnan(r["FPR"])) / max(out_s, 1)
    tpr = sum(r["TPR"] * r["occ_s"] for r in rs if not np.isnan(r["TPR"])) / max(occ_s, 1)
    still_s = sum(r["still_s"] for r in rs)
    tpr_st = sum(r["TPR_still"] * r["still_s"] for r in rs if not np.isnan(r["TPR_still"])) / max(still_s, 1)
    fa_h = sum(r["FA"] for r in rs) / max(out_s / 3600.0, 1e-9)
    flick = float(np.nanmedian([r["flicker_per_min"] for r in rs]))
    lat = float(np.nanmedian([r["latency_s"] for r in rs]))
    rel = float(np.nanmedian([r["release_s"] for r in rs]))
    return {"out_s": out_s, "occ_s": occ_s, "TPR": tpr, "TPR_still": tpr_st, "FPR": fpr, "FA_per_h": fa_h,
            "flicker_per_min": flick, "latency_s": lat, "release_s": rel}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", default=None)
    ap.add_argument("--json", default=None)
    args = ap.parse_args()
    man = load_manifest(args.manifest)
    allres = {}
    for e in man["sessions"]:
        ses = load_session(e)
        print(f"session {ses.path}: {len(ses.t)} frames")
        results, traces = evaluate(ses)
        print(f"\n{'candidate':16s} {'TPR':>6s} {'still':>6s} {'FPR':>6s} {'FA/h':>6s} {'flick/min':>9s} {'lat s':>6s} {'rel s':>6s}   (out {sum(r['out_s'] for r in results['A_schmitt'])} s, occupied {sum(r['occ_s'] for r in results['A_schmitt'])} s, leave-one-group-out)")
        for name, rs in results.items():
            if not rs:
                continue
            p = pooled(rs)
            allres[name] = p
            print(f"{name:16s} {p['TPR']:6.3f} {p['TPR_still']:6.3f} {p['FPR']:6.3f} {p['FA_per_h']:6.1f} {p['flicker_per_min']:9.2f} {p['latency_s']:6.1f} {p['release_s']:6.1f}")
        if args.json:
            Path(args.json).write_text(json.dumps({"pooled": allres, "per_group": results}, indent=1, default=float))
            print(f"wrote {args.json}")


if __name__ == "__main__":
    main()
