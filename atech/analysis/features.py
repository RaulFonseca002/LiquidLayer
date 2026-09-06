"""features.py — per-frame features for presence detection from a loaded Session (numpy).

All features are computed on the HT-LTF amplitude vectors (52 bins) of frames that have them;
LLTF-only frames get NaN. Windows are defined in seconds on the irregular arrival times and
evaluated with prefix sums (nan-aware), so no resampling is needed. Frames of a different layout
inside a window are excluded from that window's statistics (the two layouts are different channels).

    F = compute(ses, ref_mask)        # ref_mask: frames that define the "empty room" statistics
    F["jitter"], F["jitter_s"], F["var1"], F["var5"], F["wander"], F["zcount"], F["zmed"],
    F["rssi"], F["drssi"], F["layout_switch"], F["valid"]

`ref_mask` is what the training step decides (labelled `out` frames of the training windows);
templates and per-bin statistics are per layout.
"""
from __future__ import annotations

import numpy as np

from dataset import Session

EMA_ALPHA = 0.2


def _prefix_nan(x: np.ndarray):
    """Prefix sums of x and x^2 and of the valid-count, treating NaN rows as absent."""
    ok = ~np.isnan(x[:, 0]) if x.ndim == 2 else ~np.isnan(x)
    xz = np.where(np.isnan(x), 0.0, x)
    s1 = np.concatenate([np.zeros((1,) + x.shape[1:]), np.cumsum(xz, axis=0)])
    s2 = np.concatenate([np.zeros((1,) + x.shape[1:]), np.cumsum(xz * xz, axis=0)])
    c = np.concatenate([[0], np.cumsum(ok.astype(np.int64))])
    return s1, s2, c


def window_var(x: np.ndarray, t: np.ndarray, w_s: float) -> np.ndarray:
    """Mean over bins of the variance of x over the trailing w_s seconds (per frame). x: N x B."""
    s1, s2, c = _prefix_nan(x)
    j = np.searchsorted(t, t - w_s, side="left")
    i = np.arange(len(t)) + 1
    n = (c[i] - c[j]).astype(np.float64)
    n[n < 3] = np.nan
    m = (s1[i] - s1[j]) / n[:, None]
    v = (s2[i] - s2[j]) / n[:, None] - m * m
    return np.nanmean(np.clip(v, 0, None), axis=1)


def corr_rows(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Row-wise Pearson correlation of a and b (N x B); NaN where undefined."""
    a0 = a - np.nanmean(a, axis=1, keepdims=True)
    b0 = b - np.nanmean(b, axis=1, keepdims=True)
    num = np.nansum(a0 * b0, axis=1)
    den = np.sqrt(np.nansum(a0 * a0, axis=1) * np.nansum(b0 * b0, axis=1))
    with np.errstate(invalid="ignore", divide="ignore"):
        return np.where(den > 1e-12, num / den, np.nan)


def ema(x: np.ndarray, alpha: float, hold: bool = False) -> np.ndarray:
    """EMA over the non-NaN samples. With hold=False a NaN input yields NaN output (no information),
    with hold=True the last value is carried."""
    out = np.empty_like(x)
    s = np.nan
    for i, v in enumerate(x):
        if not np.isnan(v):
            s = v if np.isnan(s) else s + alpha * (v - s)
            out[i] = s
        else:
            out[i] = s if hold else np.nan
    return out


def layout_templates(ses: Session, ref_mask: np.ndarray) -> dict:
    """Per-layout template (mean normalised vector), per-bin median and MAD over the reference frames."""
    out = {}
    for lay in (2, 3):
        m = ref_mask & (ses.layout == lay) & ~np.isnan(ses.amp[:, 0])
        if m.sum() < 20:
            continue
        a = ses.amp[m]
        tpl = a.mean(axis=0)
        tpl /= np.linalg.norm(tpl)
        med = np.median(a, axis=0)
        mad = np.median(np.abs(a - med), axis=0) * 1.4826 + 1e-9
        out[lay] = {"template": tpl, "median": med, "mad": mad, "n": int(m.sum())}
    return out


def compute(ses: Session, ref_mask: np.ndarray, templates: dict | None = None) -> dict:
    n = len(ses.t)
    t = ses.t
    amp = ses.amp
    has = ~np.isnan(amp[:, 0])
    tpls = templates if templates is not None else layout_templates(ses, ref_mask)

    # same-layout previous frame within 1 s
    prev_idx = np.full(n, -1, dtype=np.int64)
    last = {2: -1, 3: -1}
    for i in range(n):
        lay = int(ses.layout[i])
        if not has[i]:
            continue
        p = last.get(lay, -1)
        if p >= 0 and t[i] - t[p] <= 1.0:
            prev_idx[i] = p
        last[lay] = i
    jitter = np.full(n, np.nan)
    ok = prev_idx >= 0
    jitter[ok] = 1.0 - corr_rows(amp[ok], amp[prev_idx[ok]])
    jitter = np.clip(jitter, 0, None)
    jitter_s = ema(jitter, EMA_ALPHA)

    # wander vs the per-layout static template; robust per-bin z-scores
    wander = np.full(n, np.nan)
    zcount = np.full(n, np.nan)
    zmed = np.full(n, np.nan)
    for lay, tp in tpls.items():
        m = has & (ses.layout == lay)
        if not m.any():
            continue
        wander[m] = np.clip(1.0 - corr_rows(amp[m], np.broadcast_to(tp["template"], (m.sum(), amp.shape[1]))), 0, None)
        z = np.abs(amp[m] - tp["median"]) / tp["mad"]
        zcount[m] = (z > 3.0).sum(axis=1)
        zmed[m] = np.median(z, axis=1)
    wander_s = ema(wander, EMA_ALPHA)

    # windowed amplitude variance (motion proxies), per layout to avoid mixing channels
    var1 = np.full(n, np.nan)
    var5 = np.full(n, np.nan)
    for lay in (2, 3):
        m = has & (ses.layout == lay)
        if m.sum() < 10:
            continue
        idx = np.where(m)[0]
        v1 = window_var(amp[idx], t[idx], 1.0)
        v5 = window_var(amp[idx], t[idx], 5.0)
        var1[idx] = v1
        var5[idx] = v5

    # RSSI dynamics and layout switching
    rssi = ses.rssi.astype(np.float64)
    j = np.searchsorted(t, t - 1.0, side="left")
    drssi = np.abs(rssi - rssi[np.clip(j, 0, n - 1)])
    layout_switch = np.zeros(n, dtype=bool)
    layout_switch[1:] = ses.layout[1:] != ses.layout[:-1]
    # blank 0.5 s after a layout switch
    sw_t = t[layout_switch]
    recent_switch = np.zeros(n, dtype=bool)
    if len(sw_t):
        k = np.searchsorted(sw_t, t, side="right") - 1
        kk = np.clip(k, 0, len(sw_t) - 1)
        recent_switch = (k >= 0) & (t - sw_t[kk] <= 0.5)
    gap = np.concatenate([[0.0], np.diff(t)])
    valid = has & ~recent_switch & (gap < 1.0)
    templated = np.zeros(n, dtype=bool)
    for lay in tpls:
        templated |= ses.layout == lay
    return {"t": t, "jitter": jitter, "jitter_s": jitter_s, "wander": wander, "wander_s": wander_s,
            "var1": var1, "var5": var5, "zcount": zcount, "zmed": zmed, "rssi": rssi, "drssi": drssi,
            "layout": ses.layout, "layout_switch": layout_switch, "valid": valid, "templated": templated,
            "label": ses.label, "templates": tpls}


FEATURE_NAMES = ["jitter_s", "wander_s", "var1", "var5", "zcount", "zmed", "drssi"]


def matrix(F: dict, names=FEATURE_NAMES) -> np.ndarray:
    """N x len(names) feature matrix (NaN where undefined)."""
    return np.stack([F[k] for k in names], axis=1).astype(np.float64)
