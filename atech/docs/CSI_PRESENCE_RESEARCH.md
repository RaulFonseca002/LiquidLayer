# WiFi CSI presence detection on a single-antenna ESP32-S3: what affects it and how robust systems cope

Research note for the Liquid node (2026-09-06). Sources at the end. Written after the amplitude
engine detected a seated person 1-2 m from the board with a large margin but flickered live in an
empty room; it explains why, and what the data-first redesign must measure.

## 1. Our setup and what we observed

One Atech ESP32-S3 station receives CSI from the ICMP echo replies of a home router (HT20, 20 Hz,
lltf/htltf/stbc with `ltf_merge_en`). Features: `jitter = 1 − corr(a_t, a_{t−1})`,
`wander = 1 − corr(a_t, a_ref)` over L2-normalised HT-LTF amplitudes.

| Observation (this flat) | Value |
|---|---|
| Empty room wander | 0.008-0.02, σ ≈ 0.009, excursions to 2× threshold for seconds |
| Seated 1-2 m from the board, on the router-board path | wander 0.7-1.1 |
| Seated at the desk, off the path | wander ≈ 0.06 |
| Router frame layout | flips 256-byte (HT) ↔ 384-byte (HT+STBC) for minutes, with RSSI −39..−58 dBm |
| HT-LTF block of a 256-byte vs a 384-byte frame | corr −0.35 (different channel estimates) |
| LLTF block against its own template (`ltf_merge_en=true`, auto gain) | wander 0.57: unusable as configured |
| Breathing spectrum while seated | no stable peak in 0.1-0.5 Hz across 30 s blocks |

## 2. What physically affects single-link detection

**Fresnel geometry.** The first Fresnel-zone radius at the midpoint of a 4 m link at 2.4 GHz is
about 50 cm; a body 1 m outside the zone perturbs the channel very little because its reflection is
swamped by the direct path. Sensitivity also depends on body orientation and on which Fresnel
ellipse the body sits in. The desk-vs-chair gap above is this effect, not a tuning defect. The
mitigation is placement (put the router-board path through the occupied area) or a coverage map
that states where detection works. [Fresnel model][fresnel1], [respiration Fresnel][fresnel2],
[dynamic Fresnel][fresnel3].

**Sampling rate.** A body crossing Fresnel zones at speed v modulates the channel at ≈ 2v/λ:
walking at 1 m/s gives ≈ 16 Hz at 2.4 GHz. At 20 frames/s (Nyquist 10 Hz) walking energy aliases.
Espressif's router example pings at 100 Hz and `esp_wifi_sensing` polls at 50 Hz; production
systems use 100 Hz. [production][origin], [CSI-Bench][csibench].

**Access-point behaviour.** A router adapts MCS/rate, STBC or antenna selection, transmit power and
beamforming to link quality. Each change is a step in the CSI amplitude profile: a single-frame
jitter spike and a new wander plateau that never returns to the template. In our recordings the
layout switch is exactly this. Mitigations: key templates on the frame type, blank the frames after a
change, require k-of-n confirmations, or use a dedicated fixed-rate sender. [802.11bf overview][bf],
[CSI survey][survey1].

**Receiver gain.** The ESP32 does not report the AGC gain; amplitudes carry an unknown time-varying
scale, and the ESP32 has the highest residual amplitude noise among devices measured (SD ≈ 0.035).
Standard remedies: normalise each frame (ℓ1 or ℓ2), divide by the long-run per-subcarrier mean
profile (the static shape is stable to > 0.9999 across days), Hampel-filter outliers. Espressif adds
`manu_scale` (fixed scaling) or the `esp-csi-gain-ctrl` component. 8-bit I/Q quantisation puts
millimetre-scale chest motion below the floor once the gain scales the signal down. [receiver
effects][rx], [esp-csi #185][agc], [Techpedia][techpedia].

**Through walls, pets, fans.** WiFi sensing sees through walls to a material-dependent degree.
In a deployment across millions of routers, 63 % of detections were non-human (pets, robot vacuums,
fans) until a gait-feature classifier brought this to 8 %. Placement under 6.5 m from the router
works well; over 10 m degrades. [production][origin], [through-wall][ttw].

**Slow drift.** Furniture, doors, curtains, HVAC and temperature move the channel over minutes;
repositioning reduces F1 by up to 21 %. The signature is a monotonic wander drift with jitter at the
noise floor, which is the most useful discriminator we have: a template that updates only while the
room is judged empty and quiet absorbs drift without absorbing people. [preprocessing][prep].

**Interference.** Contention and retries make frame arrival irregular; features computed on sample
index rather than timestamp are biased. Timestamp every frame, resample to a uniform grid, and gate
decisions on the effective frame rate.

## 3. How robust systems decide

**Espressif `esp_wifi_sensing`.** States INACTIVE → DEBOUNCE → ACTIVE with a dynamic baseline,
smoothing, adaptive noise estimation, hysteresis (separate enter and exit levels) and a hold window
(`active_filter_ms`). Motion comes from `waveform_jitter`; presence from `waveform_wander` against a
trained template, gated by `active_jitter_min` (wander alone never asserts presence unless jitter
has been non-trivial recently). Training returns both thresholds; longer calibration means fewer
false triggers. [esp_wifi_sensing][ews], [esp-radar][radar], [wifi_sensing_demo][demo].

**Production (Origin Wireless).** 100 Hz CSI, a motion statistic from the autocorrelation of CSI
over a window, sliding-window consistency before confirming a detection, a 13-feature SVM for
human vs non-human, evaluated leave-one-home-out over 7 homes without per-home retraining
(92.6 % accuracy). [production][origin].

**Academic presence systems.** Motion-event detectors with long hold times report 3-6 false alarms
per day over multi-day tests; naive multi-link averaging can make things worse (selecting the best
link beats averaging). The CSI ratio of two antennas (FarSense) removes amplitude noise and phase
offsets and is what makes far-range respiration work; it needs two receive antennas, which this
board lacks. [dilution][dilution], [FarSense][farsense].

**Thresholds.** mean + 4σ from a short window assumes Gaussian, stationary noise: on that assumption
alone it yields ~13 false frames per hour at 20 Hz, before drift. Robust systems set thresholds from
the empirical quantile of long empty recordings at a target false-alarm rate. [Wi-CCFAR][ccfar].

**What generalises across rooms vs what needs per-room calibration.** Change detectors (jitter,
windowed variance, motion-band energy) and the decision structure (Schmitt + hold + validity gates)
transfer; templates, absolute thresholds and informative-subcarrier choices do not. Cross-environment
F1 drops of 20-45 points are typical (CSI-Bench: 75 % → 52 %). [CSI-Bench][csibench],
[cross-domain survey][survey2].

## 4. Transmitter control

Espressif ranks CSI sources: router (simplest, router-dependent), device-to-device, and a dedicated
broadcast sender ("highest detection accuracy and reliability"): ESP-NOW at fixed MCS0/long GI,
fixed channel, fixed source MAC filtered at the receiver, fixed rate, optional fixed RX gain. This
removes rate/STBC/power switching and lets the sender be placed so the Fresnel ellipse covers the
occupied area. Not available here today (one board); documented as the highest-leverage future
change. [esp-csi][espcsi], [sender/receiver][s1], [router mode][s2].

## 5. Data collection protocol (adopted, scaled to the owner's time)

- Ground truth only from timestamps: the board button cycles labels live/out/still/walk with no side
  effects; `note.py` appends timestamped notes. Never label from memory.
- Every session starts with ≥ 15 min verified `out`. Overnight `out` baselines (≥ 2 nights) and a
  re-run a week later are held out and never used to fit thresholds.
- Scenarios: seated 20 min at the on-path chair, the off-path desk and a third spot; standing;
  walking; enter → 60 s still → leave cycled to ≥ 40 transitions over time; a person in the next
  room with the door closed/open; door opened/closed with nobody inside; fan/TV on; furniture moved
  then empty. Alternate the two CSI configurations (current vs LLTF-only with fixed scaling).
- Metrics: per-second TPR/FPR with ±5 s guard bands, false alarms per hour of `out`, p50/p95 latency
  at enter and (separately) release latency at leave, flicker rate, miss rate by position,
  leave-one-session-out generalisation, operating point at a target false-alarm rate.

## 6. Candidate algorithms to compare offline (ranked)

1. **Motion event + long hold** ("someone moved recently"): jitter or 0.5-2 s windowed variance or
   0.3-5 Hz band energy over median + k·MAD, k-of-n frames, presence latched 30-120 s. Immune to
   drift and to AP steps (single frames), catches micro-movements at the desk.
2. **Dynamic baseline + Schmitt + hold**: template updated only while INACTIVE and quiet (τ 5-15
   min), separate enter/exit levels, jitter gate, recent-motion memory. Absorbs drift.
3. **Per-subcarrier robust statistics**: running median/MAD per bin, count of bins beyond 3σ.
   Sensitive to the localised perturbation an off-path body produces, which correlation dilutes.
4. **Tiny learned classifier** over ~10 features to select and weight them, trained on some
   sessions, judged on held-out ones; the coefficients justify the final hand-coded rule.
5. **Current wander/jitter Schmitt** as the baseline to beat, with per-layout templates, k-of-n,
   separate enter/exit levels and a jitter gate.

## 7. Where the decision lives

Bandwidth is not the constraint: 20 Hz × 12 features ≈ 1 kB/s; raw CSI at 100 Hz ≈ 38 kB/s.
Espressif runs everything on-chip and uses the host only for diagnostics; production systems run
the base detector at the edge and heavier inference in the cloud. Our route: develop and select the
algorithm on the host from labelled recordings (fast iteration, no reflash), run the host detector
live meanwhile, then port the winner to the board so it keeps working without the laptop.

## 8. Online resources

- Espressif esp-csi: `console_test` (recording and training GUI), `wifi_sensing_demo` (Web Serial
  tuning of enter/exit levels), `csi_send`/`csi_recv` (dedicated sender). The `esp-radar` and
  `esp_wifi_sensing` components are prebuilt ESP-IDF 5.x libraries: the algorithms and protocol
  transfer to us, the binaries do not run on the Arduino-core build the Atech SDK uses.
- CSI-Bench for the labelling protocol; RuView blog for practical limits. No public dataset matches
  this board and room; we build our own.

## Sources

[fresnel1]: https://link.springer.com/article/10.1007/s42486-021-00077-z
[fresnel2]: https://dl.acm.org/doi/10.1145/3191785
[fresnel3]: https://dl.acm.org/doi/10.1145/3596270
[origin]: https://arxiv.org/html/2506.04322
[csibench]: https://arxiv.org/html/2505.21866v1
[bf]: https://arxiv.org/pdf/2310.17661
[survey1]: https://dl.acm.org/doi/fullHtml/10.1145/3310194
[rx]: https://arxiv.org/html/2605.26836v1
[agc]: https://github.com/espressif/esp-csi/issues/185
[techpedia]: https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/solution-introduction/esp-csi/esp-csi-solution.html
[ttw]: https://arxiv.org/pdf/2304.13105
[prep]: https://www.researchgate.net/publication/393206103_Pre-processing_of_CSI_signal_for_Wi-Fi_sensing-based_motion_detection
[ews]: https://components.espressif.com/components/espressif/esp_wifi_sensing/versions/0.1.1~2/readme?language=en
[radar]: https://components.espressif.com/components/espressif/esp-radar/versions/0.3.4/readme
[demo]: https://github.com/espressif/esp-csi/tree/master/examples/esp-radar/wifi_sensing_demo
[dilution]: https://arxiv.org/pdf/2602.10823
[farsense]: https://arxiv.org/pdf/1907.03994
[ccfar]: https://ieeexplore.ieee.org/document/10947215/
[survey2]: https://dl.acm.org/doi/10.1145/3570325
[espcsi]: https://github.com/espressif/esp-csi
[s1]: https://deepwiki.com/espressif/esp-csi/3.1-csi-sender-and-receiver-setup
[s2]: https://deepwiki.com/espressif/esp-csi/3.2-router-based-csi-reception
[idfcsi]: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/wifi-driver/wifi-vendor-features.html
