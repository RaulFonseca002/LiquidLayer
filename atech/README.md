# Liquid node on the Atech 14-port board

The first physical node for Liquid: an Atech ESP32-S3 motherboard that senses
the room through WiFi (RuView-compatible CSI sensing), shows vitals and the
Liquid runtime rail on two TFT screens, and talks to the host over USB in the
Atech event/action envelope.

Status: **Milestone A** (CSI proof). See `~/.claude/plans/…nygaard.md` for the
full plan; milestones B (vitals DSP, screen A, LEDs/speaker/button) and C
(screen B rail + Liquid bridge) follow.

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
[9] empty        [10] empty       [11] csi:ruview_csi (Restart)   [13] empty   [14] empty
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

Events at 1 Hz each: `csi_frame_rate` (Hz), `csi_rssi` (dBm), `csi_activity`,
`csi_presence` (0/1), `csi_link` (`unconfigured | connecting | streaming | lost`).

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

## Milestone A result (2026-09-05)

Board on the owner's 2.4 GHz network: **20 CSI frames/s sustained**, ADR-018
frames received by `csi_sink.py` from the board with 192 subcarriers, zero
sequence gaps, RSSI about -32 dBm. Activity metric responds to motion.

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
