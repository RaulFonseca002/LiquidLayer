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
