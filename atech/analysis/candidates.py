"""candidates.py — explainable presence detectors evaluated offline on labelled features.

Every candidate is `fit(F_train_list, masks) -> params` and `run(F, params) -> bool array` over the
frames of one session, sequential where a state machine is involved. Thresholds come from the
empirical quantile of the feature over labelled `out` frames (target false-alarm rate), never from
a Gaussian assumption.

  A  schmitt       current design: jitter or wander over its on-level for k frames, off below off-level for m frames + hold
  B  motion_hold   "someone moved recently": motion feature over threshold k-of-n frames, presence held HOLD_S after the last event
  C  dynamic       template updated only while absent and quiet; enter/exit levels; jitter gate; hold
  D  zscore        count of subcarriers beyond 3 MAD of the empty-room per-bin statistics; k-of-n + hold
  E  logreg        tiny logistic regression over the feature matrix (numpy), k-of-n + hold
"""
from __future__ import annotations

import numpy as np

from features import FEATURE_NAMES, corr_rows, matrix

Q_FAR = 0.999   # per-frame quantile of the out distribution used for on-levels


def _q(x: np.ndarray, q: float) -> float:
    x = x[~np.isnan(x)]
    return float(np.quantile(x, q)) if len(x) else np.nan


def _k_of_n(cond: np.ndarray, k: int, n: int) -> np.ndarray:
    c = np.convolve(cond.astype(np.int64), np.ones(n, dtype=np.int64), mode="full")[:len(cond)]
    return c >= k


def _hold(events: np.ndarray, t: np.ndarray, hold_s: float) -> np.ndarray:
    """True while within hold_s after the last event."""
    out = np.zeros(len(t), dtype=bool)
    last = -np.inf
    for i in range(len(t)):
        if events[i]:
            last = t[i]
        out[i] = (t[i] - last) <= hold_s
    return out


# ---------------------------------------------------------------- A: current Schmitt

def fit_schmitt(Fs, masks, q=Q_FAR):
    j = np.concatenate([F["jitter_s"][m] for F, m in zip(Fs, masks)])
    w = np.concatenate([F["wander_s"][m] for F, m in zip(Fs, masks)])
    return {"thrJ": _q(j, q), "thrW": _q(w, q), "offJ": _q(j, 0.9), "offW": _q(w, 0.9),
            "on_frames": 3, "off_frames": 20, "hold_s": 3.0}


def run_schmitt(F, p):
    j, w, t, valid = F["jitter_s"], F["wander_s"], F["t"], F["valid"]
    hi = ((j > p["thrJ"]) | (w > p["thrW"])) & valid
    lo = (j < p["offJ"]) & (np.isnan(w) | (w < p["offW"]))
    pres = np.zeros(len(t), dtype=bool)
    on = False; above = 0; below = 0; on_since = 0.0
    for i in range(len(t)):
        if not on:
            above = above + 1 if hi[i] else 0
            if above >= p["on_frames"]:
                on = True; on_since = t[i]; below = 0
        else:
            below = below + 1 if lo[i] else 0
            if below >= p["off_frames"] and t[i] - on_since >= p["hold_s"]:
                on = False; above = 0
        pres[i] = on
    return pres


# ---------------------------------------------------------------- B: motion event + hold

def fit_motion_hold(Fs, masks, feature="var1", q=Q_FAR, k=3, n=5, hold_s=60.0):
    x = np.concatenate([F[feature][m] for F, m in zip(Fs, masks)])
    return {"feature": feature, "thr": _q(x, q), "k": k, "n": n, "hold_s": hold_s}


def run_motion_hold(F, p):
    x = F[p["feature"]]
    cond = (x > p["thr"]) & F["valid"]
    events = _k_of_n(cond, p["k"], p["n"])
    return _hold(events, F["t"], p["hold_s"])


# ---------------------------------------------------------------- C: dynamic baseline

def fit_dynamic(Fs, masks, q=Q_FAR, tau_s=600.0, hold_s=60.0, motion_memory_s=30.0):
    """Levels from the static-template features of the training out frames; the template itself is
    rebuilt online at run time (first 15 s of the session, then updated while absent and quiet)."""
    w = np.concatenate([F["wander_s"][m] for F, m in zip(Fs, masks)])
    j = np.concatenate([F["jitter_s"][m] for F, m in zip(Fs, masks)])
    return {"enter": _q(w, q), "exit": _q(w, 0.95), "jit_on": _q(j, q), "jit_quiet": _q(j, 0.8),
            "tau_s": tau_s, "hold_s": hold_s, "motion_memory_s": motion_memory_s, "on_frames": 3, "off_frames": 20}


def run_dynamic(F, p, amp=None, layout=None):
    """Needs the raw normalised amplitudes (session.amp) and layouts to rebuild the template online."""
    t, j, valid = F["t"], F["jitter_s"], F["valid"]
    n = len(t)
    pres = np.zeros(n, dtype=bool)
    wander = np.full(n, np.nan)
    tpl = {}
    cnt = {}
    on = False; above = 0; below = 0; on_since = 0.0; last_motion = -np.inf
    for i in range(n):
        if np.isnan(amp[i, 0]):
            pres[i] = on
            continue
        lay = int(layout[i])
        a = amp[i]
        if lay not in tpl:
            tpl[lay] = a.copy(); cnt[lay] = 1
            pres[i] = on
            continue
        T = tpl[lay]
        # wander against the current template
        a0 = a - a.mean(); T0 = T - T.mean()
        den = np.sqrt((a0 * a0).sum() * (T0 * T0).sum())
        w = 1.0 - (a0 * T0).sum() / den if den > 1e-12 else 0.0
        wander[i] = w
        if not np.isnan(j[i]) and j[i] > p["jit_on"]:
            last_motion = t[i]
        moved_recently = (t[i] - last_motion) <= p["motion_memory_s"]
        quiet = not np.isnan(j[i]) and j[i] < p["jit_quiet"]
        # template learning: fast during the first 15 s of that layout (bootstrap), slow while absent & quiet
        if cnt[lay] < 300:
            g = 1.0 / (cnt[lay] + 1)
        elif not on and quiet:
            g = min(1.0, (t[i] - t[i - 1]) / p["tau_s"]) if i > 0 else 0.0
        else:
            g = 0.0
        if g > 0:
            T = T + g * (a - T); T /= np.linalg.norm(T); tpl[lay] = T
        cnt[lay] += 1
        if cnt[lay] < 300:
            pres[i] = on
            continue
        hi = valid[i] and (w > p["enter"] and moved_recently or (not np.isnan(j[i]) and j[i] > p["jit_on"]))
        lo = w < p["exit"] and (np.isnan(j[i]) or j[i] < p["jit_quiet"])
        if not on:
            above = above + 1 if hi else 0
            if above >= p["on_frames"]:
                on = True; on_since = t[i]; below = 0
        else:
            below = below + 1 if lo else 0
            if below >= p["off_frames"] and t[i] - on_since >= p["hold_s"]:
                on = False; above = 0
        pres[i] = on
    return pres, wander


# ---------------------------------------------------------------- D: per-bin robust z-count

def fit_zscore(Fs, masks, q=Q_FAR, k=3, n=5, hold_s=60.0):
    x = np.concatenate([F["zcount"][m] for F, m in zip(Fs, masks)])
    return {"thr": max(_q(x, q), 1.0), "k": k, "n": n, "hold_s": hold_s}


def run_zscore(F, p):
    cond = (F["zcount"] > p["thr"]) & F["valid"]
    events = _k_of_n(cond, p["k"], p["n"])
    return _hold(events, F["t"], p["hold_s"])


# ---------------------------------------------------------------- E: tiny logistic regression

def fit_logreg(Fs, masks_neg, masks_pos, names=FEATURE_NAMES, iters=400, lr=0.5, l2=1e-3, hold_s=30.0):
    X = []; y = []
    for F, mn, mp in zip(Fs, masks_neg, masks_pos):
        M = matrix(F, names)
        X.append(M[mn]); y.append(np.zeros(mn.sum()))
        X.append(M[mp]); y.append(np.ones(mp.sum()))
    X = np.concatenate(X); y = np.concatenate(y)
    ok = ~np.isnan(X).any(axis=1)
    X, y = X[ok], y[ok]
    X = np.log1p(np.clip(X, 0, None) * 100.0)          # compress heavy tails
    mu, sd = X.mean(axis=0), X.std(axis=0) + 1e-9
    Xs = (X - mu) / sd
    # class balance
    wpos = 0.5 / max(y.sum(), 1); wneg = 0.5 / max((1 - y).sum(), 1)
    sw = np.where(y > 0, wpos, wneg) * len(y)
    w = np.zeros(Xs.shape[1]); b = 0.0
    for _ in range(iters):
        z = Xs @ w + b
        pr = 1 / (1 + np.exp(-z))
        g = (pr - y) * sw
        w -= lr * (Xs.T @ g / len(y) + l2 * w)
        b -= lr * g.mean()
    return {"names": list(names), "mu": mu, "sd": sd, "w": w, "b": b, "p_on": 0.5, "k": 3, "n": 5, "hold_s": hold_s}


def logreg_prob(F, p):
    M = matrix(F, p["names"])
    ok = ~np.isnan(M).any(axis=1)
    X = np.log1p(np.clip(np.where(ok[:, None], M, 0), 0, None) * 100.0)
    z = ((X - p["mu"]) / p["sd"]) @ p["w"] + p["b"]
    pr = 1 / (1 + np.exp(-z))
    pr[~ok] = np.nan
    return pr


def run_logreg(F, p):
    pr = logreg_prob(F, p)
    cond = (pr > p["p_on"]) & F["valid"]
    events = _k_of_n(cond, p["k"], p["n"])
    return _hold(events, F["t"], p["hold_s"])


# ---------------------------------------------------------------- F: adaptive baseline ratio
# The access point changes its transmit mode and with it the empty-room noise level (jitter 0.005 in
# one period, 0.8 in another). Nothing absolute survives that; ratios to a tracked baseline do.
# baseline_j / baseline_w: robust running level of jitter / wander, updated fast while absent and
# quiet, slowly (tau_slow) always, so a persistent new regime with no bursts is eventually absorbed.
# Motion event: jitter > R_j * baseline_j (k-of-n). Presence: event within hold_s, or wander >
# R_w * baseline_w while a motion event happened within memory_s (a still person got there by moving).

def fit_adaptive(Fs, masks, R_j=4.0, R_w=6.0, k=3, n=5, hold_s=60.0, memory_s=120.0,
                 tau_fast_s=20.0, tau_slow_s=900.0, floor_j=0.003, floor_w=0.002):
    return {"R_j": R_j, "R_w": R_w, "k": k, "n": n, "hold_s": hold_s, "memory_s": memory_s,
            "tau_fast_s": tau_fast_s, "tau_slow_s": tau_slow_s, "floor_j": floor_j, "floor_w": floor_w}


def run_adaptive(F, p):
    t, j, w, valid = F["t"], F["jitter"], F["wander"], F["valid"]
    n = len(t)
    pres = np.zeros(n, dtype=bool)
    ratio_j = np.full(n, np.nan); ratio_w = np.full(n, np.nan)
    bj = np.nan; bw = np.nan
    hist = []               # recent event flags for k-of-n
    last_event = -np.inf; on_since = -np.inf; on = False
    for i in range(n):
        dt = t[i] - t[i - 1] if i > 0 else 0.05
        if dt > 5.0:
            bj = bw = np.nan      # gap: restart the baseline
        ji = j[i]; wi = w[i]
        if not np.isnan(ji):
            if np.isnan(bj):
                bj = max(ji, p["floor_j"])
            rj = ji / max(bj, p["floor_j"])
            ratio_j[i] = rj
        else:
            rj = np.nan
        if not np.isnan(wi):
            if np.isnan(bw):
                bw = max(wi, p["floor_w"])
            rw = wi / max(bw, p["floor_w"])
            ratio_w[i] = rw
        else:
            rw = np.nan
        ev = bool(valid[i]) and not np.isnan(rj) and rj > p["R_j"]
        hist.append(ev)
        if len(hist) > p["n"]:
            hist.pop(0)
        if sum(hist) >= p["k"]:
            last_event = t[i]
        moved = (t[i] - last_event) <= p["hold_s"]
        remembered = (t[i] - last_event) <= p["memory_s"]
        still_body = bool(valid[i]) and not np.isnan(rw) and rw > p["R_w"] and remembered
        new_on = moved or still_body
        if new_on and not on:
            on_since = t[i]
        on = new_on
        pres[i] = on
        # baseline tracking: fast while absent and quiet, slow otherwise (absorbs a new AP regime)
        quiet = not np.isnan(rj) and rj < 2.0
        tau = p["tau_fast_s"] if (not on and quiet) else p["tau_slow_s"]
        g = min(1.0, dt / tau)
        if not np.isnan(ji):
            bj = bj + g * (ji - bj)
        if not np.isnan(wi):
            bw = bw + g * (wi - bw)
    F["ratio_j"] = ratio_j; F["ratio_w"] = ratio_w
    return pres
