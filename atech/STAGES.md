# Liquid node rebuild — stage gates

Rule: a stage passes only when, after the owner presses the board's own reset
button **once**, the on-screen heartbeat keeps blinking and the frame counter
keeps counting for **60 s**, on **two consecutive presses**. Cold power-ups do
not count (they hide the warm-reset problem). Nobody opens the serial port
during a soak (opening it resets the board).

Reset reason codes shown on screen (`esp_reset_reason()`): 1 POWERON,
3 SW, 4 PANIC, 5 INT_WDT, 6 TASK_WDT, 7 WDT, 9 BROWNOUT; the S3 USB-serial
reset shows as 0/UNKNOWN.

| Stage | Content | Gate result | When | Notes |
|---|---|---|---|---|
| S0 | `st7735_tft` only, bus hygiene, heartbeat/counter/uptime/reset reason | **PASS** | 2026-09-05 22:55 | 2 board resets, heartbeat + counter alive 60 s each; counter restarts on press (real reset) |
| S1 | + `ruview_csi` WiFi-only (`csi_enable 0`), late start; link/IP/RSSI on screen | **FAIL** | 2026-09-05 23:05 | after reset: splash, then black, no frames. Split: S1a = module present, WiFi off (`wifi_enable 0`) |
| S1a | S1 with `csi_wifi_enable 0` (module present, radio never starts) | **FAIL** | 2026-09-05 23:10 | same as S1: splash, then black. Radio is NOT the trigger. Remaining runtime difference vs S0: a JSON line to USB serial every 200 ms (+ NVS read at boot) |
| S1b | S1a + all module serial I/O gated on `if (Serial)` (host actually reading) | **FAIL** | 2026-09-05 23:15 | still dies. Serial *output* is not it, but `if (Serial)` still pokes the USB driver every loop |
| — | **Rebuild v2 ladder** (one change per rung; L0 baseline carries WDT, RTC boot counter, boot card, NeoPixel loop heartbeat) | | | |
| L0 | S0 + instrumentation (WDT 5 s, boot counter, boot card, NeoPixel blink from loop) | **PASS** | 2026-09-05 23:30 | reboots cleanly on the button. Finding: the Restart-slot button pulls chip EN → chip fully powers down (RTC memory cleared, card says "COLD") while the **panel keeps power**. So RTC counter cannot tell EN-reset from power-cycle; the panel-side distinction (panel kept power) is what matters |
| L1 | L0 + `Serial.println("tick")` every 200 ms, nothing else (280 KB) | **PASS** | 2026-09-05 23:40 | serial output with no reader is innocent. Remaining: per-loop HWCDC polling (`available()`/`if (Serial)`) vs the big WiFi-linked binary — L2 first (shared by every failing build) |
| L1' | L0 + `if (Serial) {}` every loop, no prints | pending | | isCDC_Connected() pokes the USB FIFO on every call |
| L2 | L0 + `link_probe` (links WiFi/lwIP/ping, 608 KB, does nothing) | **PASS** | 2026-09-05 23:48 | binary size, boot length and the linked libraries are innocent |
| L3 | L2 + Preferences (NVS) open+read at boot | **PASS** | 2026-09-06 00:02 | NVS at boot is innocent. Left from the old module: per-loop USB-CDC polling (L3s) and 27 KB statics + global ctors (L3g); then the radio (L4) |
| L3s | L3 + per-loop `if (Serial)` + `Serial.available()/read()` | **PASS** | 2026-09-06 00:08 | per-loop USB-CDC polling is innocent |
| L3g | L3 + `bulk_probe` (old module's static footprint + WiFiUDP/Preferences ctors, idle) | **PASS** | 2026-09-06 00:14 | statics and global constructors are innocent. Every piece of the old module now passes alone → run the control |
| LC | control: instrumented base + the real `ruview_csi` (radio off, serial gated) = S1b + WDT/boot card/LED | **PASS** | 2026-09-06 00:22 | the S1b failure does NOT reproduce on the instrumented base. Candidates for what fixed it: 2 s boot-card pause after display init (LCnd tests), NeoPixel blink from loop, WDT |
| LCnd | LC without the 2 s boot-card pause | **PASS** | 2026-09-06 00:28 | pause not needed. NOTE: the `st7735_tft` override (extra SWRESET before init) has been active in every build since L3 (created 23:38) — a confound and a fix candidate |
| LCndno | LCnd built with the STOCK st7735 driver (single SWRESET) — no override | **PASS** | 2026-09-06 00:33 | double SWRESET is NOT the fixer. Left vs S1b: NeoPixel present + blinking from loop, loop WDT, boot card frame/RTC counter |
| LCndno-noLED | LCndno without the NeoPixel module (= S1b + WDT + boot card) | **PASS** | 2026-09-06 00:38 | the LED is irrelevant |
| LCndno-noWDT | LCndno without enableLoopWDT (= S1b + LED + boot card) | **FAIL (intermittent)** | 2026-09-06 00:45 | screen sometimes freezes on the boot card right after setup; sometimes runs. → the failure is an early-boot **hang in loop()**; the 5 s loop WDT was masking it by rebooting (a retry usually succeeds). All L0+ "passes" with WDT are therefore suspect |
| LH | hang locator: stock driver + WDT + RTC progress markers (csi/tpl/loop) shown on the boot card after a reason-6 reboot | pending | | pins the exact step where loop() hangs |
| L4 | L3 + WiFi station late start + 1 Hz UDP alive beacon from loop | pending | | radio; beacon = loop liveness over the network |
| L5 | L4 + CSI arm + ring + DSP task (no UDP) | pending | | |
| L6 | L5 + ADR-018 + vitals UDP; sink ≥15/s, drops <5 % | pending | | |
| L7 | vitals view, button, LED pulse, speaker — one per flash | pending | | |
| S2 | + CSI capture, DSP task on core 0, UDP; Hz on screen; `csi_sink.py` ≥15/s, drops <5 % | pending | | |
| S3 | + vitals view, button on port 9 swaps views | pending | | |
| S4 | + NeoPixel pulse (throttled, after render) + speaker tick | pending | | |
| S5 | Milestone C: second TFT rail + Liquid bridge | deferred | | separate plan |


## Phase 1 — hang locator (2026-09-06)

Faithful S1b config (display + ruview_csi, radio off) + monotonic RTC marker `g_hlPhase`, each step painted to the panel so a freeze shows its phase; boot card shows PREV phase, reset reason, and a WDT-reboot tally. Watchdog ON only to auto-recover between trials; a WDT reboot is logged as a masked hang.

Phase legend: 90 begin-enter, 92 after loadConfig(NVS), 93 after subscribe, 94 begin-done; 95 post-begin(user setup), 96 pins, 97 card shown, 98 setup-end(return); 101 loop/update entry (before Serial touch), 102 after (bool)Serial, 103 after poll, 104/105 update tail; 110 loop entry, 111 past 2Hz gate, 112/113 render.

FINDING (tooling): the SDK build tree (`<project>/build/lib/`) is NOT purged between builds, so a renamed/removed module lingers and the linker can bind the WRONG object (two modules both defining `RuViewCsi` → old one linked, `undefined reference to g_hlPhase`). Fixed by clearing stale module libs before build. This may have contaminated any earlier rung that reused the liquid-node build dir after a module-set change — treat borderline earlier passes with suspicion; the Phase 1 data below is from a purged build.

| Locator run | PREV phase | reason | wdt tally | reading |
|---|---|---|---|---|
| (pending owner) | | | | |

### Phase 1 result (2026-09-06 ~01:35) — ROOT CHARACTERISED: boot-time settle race

Controlled flashes, all clean/purged builds, watchdog OFF (a hang freezes visibly):
- S1b control (stock display + original ruview_csi, radio off, minimal setup, delay 50 ms): logo then black, FROZEN. Bug is real and reproducible in a clean build (the build-dir purge did NOT fix it).
- NS control (same, but ALL Serial/HWCDC access removed from the module loop path): identical freeze. USB-CDC/serial is NOT the cause.
- Locator (ruview_csi_hl, radio off, WDT off, delay 2500 ms before loop): reaches "locator RUNNING" reliably.
- No-delay locator (identical, delay cut to 50 ms): freezes on the boot card, never reaches loop.

Conclusion: with the WiFi/lwIP stack linked, starting loop() too soon after boot deadlocks the board, independent of serial, radio (off), and CSI logic (does nothing). A ~2.5 s settle before the first loop makes it reliable; ~50 ms does not. Ruled out this session: display driver / setRotation / double-SWRESET, NeoPixel/RMT, speaker, button, watchdog, binary size, NVS-at-boot (begin completes: the black frame paints after it), serial output, per-loop serial polling, and build-dir contamination (separate tooling bug, fixed).

Open: identify WHAT must be ready (a background task/timer/one-time init from the linked stack). Next: delay-threshold sweep (500/1000/1500/2000 ms) to find the settle boundary and implicate the actor, then replace the blind delay with a wait-on-actual-ready condition (the real fix, not a delay).

### ROOT CAUSE CONFIRMED (2026-09-06 ~02:00): USB-CDC-on-boot deadlock, no host

Matched-control proof (only one variable changed):
- s1b control, ARDUINO_USB_CDC_ON_BOOT=1, WDT off, no USB host: froze reliably (logo then black).
- SAME firmware, ARDUINO_USB_CDC_ON_BOOT=0 (Serial.setTxTimeoutMs line disabled since it is HWCDC-only), WDT off, no host: 15 cold resets, ZERO freezes.

Root cause: the ESP32-S3 USB-Serial-JTAG HWCDC driver, auto-initialised at boot by ARDUINO_USB_CDC_ON_BOOT=1, intermittently deadlocks during early startup when NO USB host is attached, in the heavier WiFi-linked build. Corroborating evidence gathered this session: a USB host attached (serial monitor / logger) always prevented the freeze (steady SOF stream keeps HWCDC connection-detection stable); removing the user's own serial calls did NOT help (the core calls Serial.begin regardless); a longer pre-loop delay only shifted the probability; the loop watchdog masked it by rebooting. The Atech SDK hardcodes ARDUINO_USB_CDC_ON_BOOT=1 in its generated platformio.ini.

Cost of the raw fix: with CDC_ON_BOOT=0, `Serial` is UART0, not USB — so `atech monitor`/`send` over USB break. Production fix must KEEP USB serial: build CDC_ON_BOOT=0 and bring the USB-CDC up DELIBERATELY after boot/settle (USBSerial/HWCDC begin from setup once past the fragile window), or equivalently defer/guard the CDC init. To design + validate next, then report upstream to Atech.

## Rebuild v3 on the confirmed fix (2026-09-06 ~02:50)

THE FIX (skill pipeline, applied to every build): scripts/build_flash.py now
generates -> purges stale build/lib+src (prevents linker binding a wrong stale
module object) -> sets ARDUINO_USB_CDC_ON_BOOT=0 -> routes the generated code's
Serial to atechUsb() (scripts/atech_usb.h), which brings the USB-CDC up ~2 s
after boot from loop() instead of at boot. UART0 (port-11 pins) never starts.
deploy.sh drives build_flash for both build and upload; --stock-usb reproduces
the SDK default. Verified on hardware: hardened firmware boots reliably, health
event reports usb_up=1 usb_host=1, USB events flow after the deferred bring-up.

NODE: ruview_csi kept (verified DSP), watchdog OFF, wifi_enable/csi_enable made
runtime-only (a stale "off" in flash must not survive a reflash), fall gated on
presence, +40-byte NodeStatus beacon (magic 0xA11E0002) sent 1 Hz from loop()
for headless liveness, +health event, IP guards (no streaming/beacon without a
real IP). Diagnostic probe modules and unproven drafts removed (in git history).

HOST: atech/host/ruview_packets.py (shared CSI/vitals/status parser),
liquid_bridge.py (node UDP -> Liquid ExternalObservation NDJSON; --selftest
passes: 17 obs, monotonic revisions, correct keys), csi_sink.py decodes status
and flags task-WDT reboots.

VERIFIED tonight: fix boots reliably; simulator passes; USB events work; CSI
streamed to the host at 20/s at 02:41 on this hardened firmware.
PENDING (needs owner / stable WiFi):
  1. Acceptance gate: >=15 cold resets (Restart button), WDT off, zero freezes,
     on-screen "wdt" count stays 0 and "r" reads 1. Owner presses.
  2. Live end-to-end re-verify (CSI >=15/s + status/vitals via liquid_bridge):
     blocked after 02:44 by WiFi failing to associate despite "Fonseca" at -35 dBm
     (13 networks, crowded ch6) — an association wedge after many rapid reflash
     resets, not a code change (same firmware streamed at 02:41). A power cycle
     and a few minutes should clear it; then run:
       python3 atech/host/csi_sink.py --seconds 60
       python3 atech/host/liquid_bridge.py --seconds 60

## TRUE ROOT CAUSE (2026-09-06 ~10:20): setTxTimeoutMs(0), not CDC-on-boot

Correction to the earlier "CDC-on-boot deadlock" finding. The deeper, proven
cause of every frozen board is the Atech SDK's generated `Serial.setTxTimeoutMs(0)`.
HWCDC::write() seeds its unplug-detection counter `tries` from tx_timeout_ms;
with 0 that counter underflows on the first stalled write, so the built-in
"host isn't reading -> give up and drop" escape (tries==0 -> connected=false)
NEVER fires. The instant the firmware writes an event to USB with no host
draining the buffer, the buffer fills and `while(connected && to_send)` spins
forever. A monitor attached drains the buffer -> never fills -> never hangs,
which is why it always looked fine while watching. CDC-on-boot only controlled
WHEN the first unguarded write happened (boot vs the deferred 2 s bring-up).

PROVEN by isolation: t1 test (display + module, WiFi auto-start disabled, USB
brought up at 2 s) froze at ~2 s standalone with tx_timeout=0; the ONE change
tx_timeout 0 -> 10 ms -> runs clean standalone across repeated cold resets.

THE FIX (scripts/atech_usb.h): AtechUsb::update() calls setTxTimeoutMs(10) (a
bounded value, MUST be > 0) after begin(). Kept alongside CDC_ON_BOOT=0 +
deferred bring-up (defensive: keeps USB init out of the fragile boot window),
but the timeout is the operative fix. Applied to the node and every skill build.

Node reflashed 10:19. PENDING: owner runs the >=15 cold-reset gate on the REAL
node (Restart button, standalone/no monitor, WDT off): counter keeps climbing,
heartbeat keeps blinking, on-screen "wdt" stays 0, "r" reads 1.


## Sensing fix (2026-09-06): presence never asserted

Symptom: after calibration the presence lamp stayed dark and screen A showed `HR --` /
`BR --` with the owner in the room. Evidence (scratchpad `follow.log`, `board.log`): 1696
presence samples, all 0; rate 19.5-20 Hz, RSSI -33..-67 dBm, 192 subcarriers, zero gaps,
build objects identical to sources. Root cause in `ruview_edge.cpp` (old): motion = mean
|Δphase| of raw CSI phase, uniformly random per packet on one ESP32 (1.39 rad ≈ π/2);
threshold learned on the raw value but compared against an EMA (13 σ bar) and above the
metric's π ceiling. Fix: amplitude-correlation engine (see README, Milestone B).

| Rung | What | Result | When | Evidence |
|------|------|--------|------|----------|
| F1 | button segments + edge diagnostics + `.csirec` recorder, old engine | **PASS** | 11:41 | flashed (`9a401aa`); `csi_edge` shows mean 1.26 σ 0.50 after 40 frames (threshold heading above π); recorder labels frames by NodeStatus segment |
| F2 | amplitude engine, native synthetic tests | **PASS** | 11:55 | `tests/edge/run.sh`: random phase → no motion; still person 99.8 % presence, BR 15.0-15.8 bpm conf ≥0.43; walk 100 %; leaves → off in 2.8 s; AGC ×1.5 → 0 %; button calibration; noise-only HR conf 0.13 |
| F3 | F2 flashed, first live calibration | **PASS** (diagnostic) | 12:03 | auto-calibration closed at 60 s; the provisional constant lit the lamp during "empty" (fixed: no presence decisions while calibrating) |
| F4 | owner ran the button protocol (empty 194 s, still 192 s, walk 93 s); replay through the engine | **PASS with limits** | 12:22 | `tests/fixtures/room-20260906-protocol.csirec`; replay: empty **0.0 %**, still **99.9 %** (wander 0.74 vs threshold 0.062, ambient 0.018), walk 58.6 % — the AP sent only 256-byte frames while walking and that layout had no template (no such frames in the empty run), so jitter alone decided. Breathing: no two 30 s blocks agree on a prominent peak → BR stays `--` (honest). 128-byte LLTF-only frames (1.4 %) are ignored: their LLTF block is noise even against its own template |
| F5 | hardware acceptance with the final engine (lamp within 3 s, still 2 min, off within ~5 s, no FALL, wdt 0) | pending owner | 12:40 flashed | recorder still running; a second protocol run with the final engine is the gate |

Findings that changed the design during F4:
- 256-byte (HT) and 384-byte (HT+STBC) frames carry HT-LTF blocks of *different* channels (corr −0.35): per-layout templates and previous frames, shared noise statistics, lazy template for a layout unseen during calibration (only while a templated layout judges the room empty and quiet).
- The LLTF block is unusable (wander 0.57 against its own template); only the HT-LTF block is used.
- Autocorrelation confidence passed 25 % of empty-room frames as "confident breathing". Replaced by a Hann block DFT per bin, per-bin normalised and fused over the band: prominence = peak/median; a reading needs prominence ≥ 3 off the band edges in two consecutive blocks agreeing within 1.5 bpm. The owner's still run peaks at the 6 bpm edge (drift) or wanders (11, 21, 16 bpm): nothing to report, and the engine says so.
- Real idle jitter is 0.037 ± 0.013 and idle wander 0.020 ± 0.009 (the synthetic room sits at 0.006 / 0.003); constants must be tuned from recordings, never from the synthetic tests.

## Data-first redesign (2026-09-06 14:00 →)

Live tuning failed because the empty room is non-stationary: the router flips 256/384-byte frame
layouts with link quality and changes transmit mode (empty-room jitter 0.005 in one period, 0.8 in
another); a seated person off the router-board path barely registers (Fresnel geometry). Decision:
learn from labelled recordings, propose the algorithm from data, keep the board a labelled sensor.
Research note: `docs/CSI_PRESENCE_RESEARCH.md`. Toolkit: `analysis/` (numpy in the atech venv).

| Rung | What | Result | Evidence |
|------|------|--------|----------|
| G1 | firmware: label-only button (live/out/still/walk), collection screen, frame flags + rate in ADR-018 byte 18/19, 50 Hz pings (30-35 fps observed), `csi_cfg` LLTF-only experiment mode | **flashed 14:00** (`5a80093`) | `csi_edge`/screen show label + raw numbers, no verdict |
| G2 | dataset: `analysis/sessions.json` (3 button protocols + 13:31 out run = 1011 s out, 509 s still, 323 s walk certain), `dataset.py`, `record.sh`, `note.py` | done | loads 164k frames in 1 s |
| G3 | offline evaluation, leave-one-protocol-out, per-group templates from the first half of each out window | **done** | protocols 1+2: F adaptive ratio occupied 100 % / out 0 % / 0 FA; A current Schmitt 1-77 %; motion-only B misses a seated person (0-35 % still); protocol 3: every detector fails — router mode flapped every ~40 s after the 12:57 reflash (empty jitter 0.15↔1.6); 13:31 out run: 0 % for all. Report artifact "Room Presence Evaluation" |
| G4 | `host/presence_host.py` live (adaptive ratio + hold, records .csirec, CSV, optional ExternalObservation) | running since 14:15 | `~/.atech/recordings/<date>/presence_host.log` |
| G4b | on-device port of the adaptive-ratio detector (warm-up 15 s, per-layout templates, tracked baselines, motion events 3-of-5 > 4x, hold 60 s, wander > 6x within 120 s of an event; verdict OUT / IN still / IN moving on screen; no stored calibration) | **flashed** | synthetic suite green; protocol 1 replay: empty 0.0 %, still 100 %, walk 100 %; protocol 2 informational: two channel bursts 30 s and 90 s after leaving hold presence (through-wall/corridor), still 97.5 %, walk 99 %; a 5x noisier router regime is absorbed within ~5 min |
| G5 | next sessions: overnight out baselines, 3 seat positions, ≥40 enter/leave transitions, LLTF-only config sessions; key baselines on the router mode (rate/STBC flags now logged) | pending owner | |
