# Liquid node on the Atech 14-port board

The first physical node for Liquid: an Atech ESP32-S3 motherboard that senses
the room through WiFi (RuView-compatible CSI sensing), shows vitals and the
Liquid runtime rail on two TFT screens, and talks to the host over USB in the
Atech event/action envelope.

Status: **Milestone B done** — CSI proof (A) plus on-device heart-rate/breathing
vitals, screen A, and NeoPixel/speaker/button (B). Milestone C (screen B Liquid
rail + host bridge) is next. See `~/.claude/plans/…nygaard.md` for the full plan.

This folder is deliberately outside Liquid core (`include/`, `src/`, `apps/`,
`tests/`) and adds nothing to the CMake build. A later round turns it into an
installable Liquid module with an adapter registered in `SystemPhase::Input`.

## Layout

```
atech/
  modules/ruview_csi/      custom Atech module: WiFi CSI capture, ADR-018 UDP stream,
                           activity/presence, NVS credentials, serial actions
                           (atech_actions.h = serial action bus shared by our modules)
  projects/liquid-node/    project.yaml driven by the /atech skill (modules_path: ../../modules)
  host/csi_sink.py         RuView-compatible UDP sink for validation (stdlib)
  host/ruview-server.sh    run RuView's real sensing server in Docker (UI :3000, UDP :5005)
```

## Board (14port)

```
[9] btn:button   [10] led:neopixel [11] csi:ruview_csi (Restart)  [13]+[14] spk:speaker
[7] empty                                                                        (USB-C)
[1]+[2] tftA:st7735_tft            [3] empty  [4] empty  [5] empty  [6] empty
```

`ruview_csi` needs no wires; it only occupies port 11.

## Flash and configure

```bash
S=~/.claude/skills/atech/scripts
bash $S/deploy.sh atech/projects/liquid-node --port /dev/ttyACM0          # validate, build, flash

# credentials and sink live in the board's NVS, never in this repo
~/.atech/venv/bin/atech send csi_set_wifi '{"ssid":"<2.4 GHz SSID>","pass":"<password>"}' --port /dev/ttyACM0
~/.atech/venv/bin/atech send csi_set_sink '{"ip":"<host LAN ip>","port":5005,"node_id":1}' --port /dev/ttyACM0

# watch events (and see action acks on the same connection)
bash $S/atech-env.sh python $S/monitor.py --seconds 20 --send csi_calibrate null
```

Events (paced, ~5/s cycling): `csi_frame_rate` (Hz), `csi_rssi` (dBm),
`csi_heart_rate`/`csi_breathing_rate` (bpm, heuristic), `csi_activity`,
`csi_presence` (0/1), `csi_link` (`unconfigured|connecting|streaming|lost`).

## Prove it is a RuView node

```bash
python3 atech/host/csi_sink.py --seconds 60        # exit 0 if >= 15 CSI frames/s arrive
atech/host/ruview-server.sh start                    # RuView's real server (Docker)
```

The wrapper starts `ruvnet/wifi-densepose` with `CSI_SOURCE=esp32`, the UDP
data plane bound to all interfaces with a private-LAN source allowlist
(`RUVIEW_UDP_ALLOW`), and a per-machine API token kept in
`~/.atech/ruview_token`; the image refuses to start without one. UI at
`http://localhost:3000/ui/index.html`; API needs `Authorization: Bearer <token>`:

```bash
python3 -c 'import json,urllib.request;T=open("'$HOME'/.atech/ruview_token").read().strip();print(urllib.request.urlopen(urllib.request.Request("http://localhost:3000/api/v1/nodes",headers={"Authorization":"Bearer "+T})).read().decode())'
```

Result on 2026-09-05: the server lists the Atech board as `node_id 1`,
`rssi_dbm -32`, `motion_level present_moving`, `person_count 1`, i.e. it is
indistinguishable from a stock RuView node.

## Results (2026-09-05)

**Milestone A** — board on the owner's 2.4 GHz network: 20 CSI frames/s
sustained, ADR-018 frames received with 192 subcarriers, zero sequence gaps,
RSSI about -32 dBm. RuView's own Docker sensing server lists it as a live node
(`node_id 1`, `present_moving`, `person_count` valid).

**Milestone B** — on-device sensing (`modules/ruview_csi/ruview_edge`). The
first version was a port of RuView `edge_processing.c` (phase-based Welford
presence, zero-crossing BPM). It never detected anyone: on a single ESP32 the CSI
phase is uniformly random between packets (measured mean |Δphase| 1.39 rad, the
π/2 noise value), and its learned threshold landed above the metric's ceiling, so
presence stayed 0 in every capture and HR/BR, gated on presence, stayed `--`. The
"70-90 BPM live" readings of that version were the band-pass centre frequency on
noise, not a heartbeat. Retired 2026-09-06 (tag `atech-pre-amplitude-rewrite`).

The engine now follows Espressif's esp-radar formulation on subcarrier
**amplitude**: jitter (`1 − corr` with the previous frame) for motion and wander
(`1 − corr` with an empty-room template) for presence, thresholds learned as
mean + 4 σ during an empty-room calibration (60 s automatic at boot, or the
button-driven "empty" segment), Schmitt trigger with confirmation and hold.
Each frame layout (256-byte HT, 384-byte HT+STBC) has its own template because
their HT-LTF blocks describe different channels; the access point switches
between them with link quality. Breathing comes from a per-bin block DFT of the
detrended amplitudes fused over the band: a reading needs a prominent peak
(≥ 3× the spectrum median, off the band edges) in two consecutive 30 s blocks
that agree within 1.5 bpm. In the owner's room (2026-09-06, seated 1-2 m away)
no such peak exists, so BR reads `--`; heart rate is best-effort and is always
shown with its confidence. Fall detection is disabled (never validated).
Validated with the native harness `tests/edge/run.sh` (synthetic scenarios plus
replay of `tests/fixtures/*.csirec` with acceptance gates: empty 0.0 %, still
99.9 %). Hardware acceptance status lives in `STAGES.md` ("Sensing fix").

Button protocol (port 3): press → *empty* (leave; the baseline is relearned for
as long as the segment lasts, min 30 s after a 10 s leave delay) → press →
*still* (sit 1-2 m away, breathe normally) → press → *walk* → press → *live*.
The segment is shown on screen, logged, and carried in the NodeStatus beacon so
the recorder labels frames without touching USB.

## Rebuild rules (after the frozen-display incident, 2026-09-05)

The node is being rebuilt stage by stage; see `STAGES.md` for the gates. What
the incident taught, now design rules:

- A warm chip reset does not reset the TFT panel (no reset line, keeps power);
  only a cold power cycle does. So every stage is gated on **warm resets**:
  press the board's reset, the heartbeat must stay alive for 60 s, twice.
- `loop()` only renders (≤2 Hz) and polls actions. All CSI work (ring drain,
  DSP, UDP, vitals packet) runs in a dedicated task on core 0 and publishes a
  snapshot; `loop()` reads it. This also removes the 70 % ring drops.
- WiFi starts 1.5 s after boot so the first frame paints before the radio.
  `csi_enable` (NVS, action `csi_csi_enable 0|1`) allows a WiFi-only stage.
- Nobody opens the serial port during a soak; opening it resets the board.
  Use `monitor.py --follow` at announced checkpoints only.
- A user-side `setRotation()` on the panel was an early suspect; it was not
  the root cause but stays linted as a documented trouble spot.

## Upstream notes (RuView, MIT)

Ported from `firmware/esp32-csi-node`: CSI config (S3 legacy layout), 50 Hz
early gate, ADR-018 frame and 32-byte vitals packet layouts.

**Where this port deliberately differs from RuView**, because the Atech SDK
builds on Arduino core 2.0.17 (ESP-IDF 4.4) instead of ESP-IDF 5.4:

- **Promiscuous mode silences the CSI callback completely** on this core, with
  or without a promiscuous RX callback, MGMT-only or not. RuView relies on
  promiscuous MGMT capture; we keep it off (`csi_promisc 0`, the default) and
  take CSI only from frames addressed to the station.
- **Traffic source is an ICMP ping session to the gateway at 20 Hz**
  (`esp_ping`, the same mechanism as Espressif's esp-csi examples). Each reply
  carries CSI, giving a steady 20 frames/s. Probe-request injection remains as
  an automatic fallback when the rate drops under 12 Hz.
- The chip must not hold a stale station config: the hosted Atech firmware
  leaves one behind and the driver auto-connects to it at start, which blocks
  scans and confuses association. The module clears it before scanning.

## Atech SDK notes

- The open SDK declares actions but generates no serial reader; `atech_actions.h`
  provides one. Values arrive JSON-encoded twice (`"value":"{\"ip\":…}"`), the bus unescapes them.
- `Serial.setTxTimeoutMs(0)` from codegen means a burst of lines overflows the
  USB-CDC buffer and drops bytes; modules must pace output (one line per loop pass).
- `atech send` closes the port immediately, so acks are only visible in a
  persistent monitor (`monitor.py --send`). **Opening the serial port resets
  the board** (USB-JTAG-serial DTR/RTS), so every monitor session starts with a
  reboot and a WiFi reconnect (about 3 s).
- The SDK's `Action` model rejects `null`; send `1` for value-less actions
  (`csi_scan 1`, `csi_calibrate 1`).
- The device name can change after a reset (`/dev/ttyACM0` → `/dev/ttyACM1`).
  Use `monitor.py --follow` / `board_logger.py` to observe across resets.
- `Speaker::begin()` leaves `mck_io_num` unset (0), so I2S claims GPIO0 as MCLK.
  Harmless for us; worth an upstream report.
- `(Restart)` on the layout is the chip's EN line: a button module plugged there
  is only a hardware reset (the sensing calibration survives WiFi re-association
  but not a reset). The protocol button goes on a real port (3).
