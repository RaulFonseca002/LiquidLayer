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
| L1 | L0 + `Serial.println("tick")` every 200 ms, nothing else (~272 KB) | pending | | USB-CDC activity from loop is the top suspect (only thing shared by S1/S1a/S1b and absent in S0) |
| L1' | L0 + `if (Serial) {}` every loop, no prints | pending | | isCDC_Connected() pokes the USB FIFO on every call |
| L2 | L0 + empty module that only `#include <WiFi.h>` | pending | | binary size / boot-time / linked libs |
| L3 | L2 + Preferences read at boot | pending | | NVS |
| L4 | L3 + WiFi station late start + 1 Hz UDP alive beacon from loop | pending | | radio; beacon = loop liveness over the network |
| L5 | L4 + CSI arm + ring + DSP task (no UDP) | pending | | |
| L6 | L5 + ADR-018 + vitals UDP; sink ≥15/s, drops <5 % | pending | | |
| L7 | vitals view, button, LED pulse, speaker — one per flash | pending | | |
| S2 | + CSI capture, DSP task on core 0, UDP; Hz on screen; `csi_sink.py` ≥15/s, drops <5 % | pending | | |
| S3 | + vitals view, button on port 9 swaps views | pending | | |
| S4 | + NeoPixel pulse (throttled, after render) + speaker tick | pending | | |
| S5 | Milestone C: second TFT rail + Liquid bridge | deferred | | separate plan |
