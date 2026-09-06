# /atech — Claude Code skill for Atech hardware

Type an idea, get an assembly guide and a flashed board:

```
/atech a lamp that turns green when I press the button
```

The skill picks modules from the Atech catalog, tells you which port each one
goes in (with an ASCII picture of the motherboard), writes the firmware
behaviour, compiles it, flashes the Atech board connected over USB, and shows
the first live events.

It runs entirely on the open-source Atech SDK (`pip install atech`, MIT).
Nothing talks to atech.dev; builds and flashes work offline after a one-time
toolchain download.

## Install (any machine)

Requirements: Claude Code, Python 3.10+ (or `uv`), a USB-C data cable, and
`g++` if you want the pre-flight simulation (recommended).

```bash
tar xzf atech-skill-*.tar.gz        # or unzip atech-skill-*.zip
cd atech && ./install.sh            # copies to ~/.claude/skills/atech
```

Or copy the `atech` folder by hand to `~/.claude/skills/atech` (every
project) or `<your-project>/.claude/skills/atech` (one project). Start Claude
Code and type `/atech <idea>`.

Nothing else to set up. The first `/atech` call creates `~/.atech/venv` and
installs the SDK there (about a minute); the first build fetches the ESP32-S3
toolchain via PlatformIO (one to two minutes). Everything after that is fast.

Linux only: if flashing fails with "permission denied" on `/dev/ttyACM0`, add
yourself to the serial group once: `sudo usermod -aG dialout $USER`, then log
out and in.

## What ends up where

| Path | Purpose |
|---|---|
| `~/.atech/venv` | isolated Atech SDK + PlatformIO |
| `~/.atech/projects/<slug>/project.yaml` | one folder per idea; edit and rerun freely |
| `~/.atech/projects/<slug>/build/` | generated PlatformIO tree and `firmware.bin` (regenerated, do not edit) |
| `~/.atech/projects/<slug>/sim/` | host simulation build: generated tree, auto-mocks, `firmware_sim` (regenerated) |
| `~/.atech/logs/` | full build/flash logs per run |
| `~/.atech/default_board` | remembered board model (`8port`, `10port`, `14port`) |

Override the root with `ATECH_HOME=/some/dir`.

## Test before you flash

`simulate.py` compiles the exact firmware `atech build` produces, but for your
computer, against a fake Arduino core. Drivers that only need GPIO (button,
motors, PIR) run for real; drivers that need vendor libraries (LEDs, displays,
I2C sensors) are auto-mocked from their headers and every call is recorded.
You script the inputs and read what the code did:

```bash
S=~/.claude/skills/atech/scripts
bash $S/atech-env.sh python $S/simulate.py ~/.atech/projects/green-lamp \
  --scenario "100 press btn; 400 release btn; 700 press btn; 1000 release btn" --bounce 3 \
  --expect "led.setAll=1,led.clear=1,led.show=2"
```

```
t=100    call   led.setAll(0, 255, 0)
t=100    call   led.show()
t=700    call   led.clear()
t=700    call   led.show()
# calls: led.setAll x1 ...
expect: ok
```

It needs `g++` (Linux: `build-essential`; macOS: Xcode command line tools).
Compile errors in your `code:` block show up here instead of on the board, and
a static check warns about known traps, such as calling `wasPressed()` when
the module template already consumes it every loop.

## Using the scripts by hand

```bash
S=~/.claude/skills/atech/scripts
bash $S/atech-env.sh ports                                  # which USB device is the board
bash $S/atech-env.sh python $S/catalog.py --board 8port     # boards, modules, layout, pins
bash $S/atech-env.sh python $S/catalog.py button neopixel   # full C++ API of those modules
bash $S/atech-env.sh python $S/describe_project.py ~/.atech/projects/lamp
bash $S/deploy.sh ~/.atech/projects/lamp --port /dev/ttyACM0 --monitor 8
bash $S/atech-env.sh python $S/monitor.py --seconds 10      # just listen
bash $S/atech-env.sh send led_fill '{"r":0,"g":255,"b":0}'  # send an action
bash $S/atech-env.sh --upgrade                              # newer SDK
```

`atech-env.sh` forwards any other argument to the SDK CLI (`list-modules`,
`new`, `validate`, `build`, `upload`, `monitor`, `send`, `check`, `free`).

## Files

- `SKILL.md` — the instructions Claude follows (discover → design → validate → build → flash → report)
- `scripts/atech-env.sh` — self-bootstrapping wrapper around the SDK venv
- `scripts/catalog.py` — module/board discovery incl. each module's published C++ API
- `scripts/describe_project.py` — ASCII board layout + module→port table + placement check
- `scripts/deploy.sh` — validate → build → upload → listen, with logs and failure hints
- `scripts/monitor.py` — bounded, cross-platform event listener (`--send` acks on the same connection, `--follow` reconnecting)
- `scripts/board_logger.py` — follow the board across resets and device renames
- `scripts/simulate.py` — host-side firmware simulation with scripted inputs and call expectations
- `scripts/atech_lint.py` — static checks on the `code:` block (shared by describe and simulate)
- `scripts/sim/` — fake `Arduino.h`, `esp_system.h` and the recording runtime used by the simulator

## Findings worth knowing (Atech SDK 1.0.0a7, 14-port board, 2026-09-05)

Learned the hard way while building a WiFi-sensing node; each one is now a
rule or a check in this skill.

- **Display:** the ST7735 160x80 panel keeps power across a chip reset and has
  no reset line to the chip, so a warm reset can leave it showing only the
  Atech boot splash while the firmware runs on. A cold power cycle hides the
  problem. Rebuild stage by stage, gate with warm resets, keep `loop()` short,
  draw a heartbeat so "frozen" is visible. (A user-side `setRotation()` was an
  early suspect; it was not the root cause but stays linted as a trouble spot.)
- **Serial resets the board:** opening the USB port restarts the chip (S3
  USB-Serial-JTAG, not disable-able) and the device may come back under a new
  name. `monitor.py --follow` reconnects across both.
- **Actions:** the open SDK declares actions but generates no serial reader
  (our modules ship `atech_actions.h`); values arrive JSON-encoded twice; the
  SDK rejects `null`, so send `1`.
- **USB TX timeout (the big one):** the SDK's generated `Serial.setTxTimeoutMs(0)`
  makes the ESP32-S3 USB write loop spin *forever* the instant the board writes
  with no host reading (the TX buffer fills, its unplug-detection counter
  underflows, and it never gives up). This froze the board on every standalone
  boot — the whole "frozen display" saga. The pipeline (`build_flash.py` +
  `atech_usb.h`) fixes it: a bounded non-zero TX timeout, USB brought up after
  boot, `ARDUINO_USB_CDC_ON_BOOT=0`. So always flash through `deploy.sh`; a raw
  `atech build` reintroduces the hang. Still emit at most one event line per
  ~200 ms to keep the buffer healthy. **This is the #1 bug to report upstream.**
- **Button template:** it consumes `wasPressed()` before user code; poll
  `isPressed()` and detect edges yourself.
- **Reset footprint:** the `(Restart)` position is the EN line, not a port. A
  button module there is only a hardware reset.
- **Speaker driver:** `Speaker::begin()` never sets `mck_io_num`, so I2S claims
  GPIO0 as MCLK (harmless today, worth reporting).
- **Radio:** on Arduino core 2.0.17, promiscuous mode silences the CSI callback;
  CSI only comes from station-addressed frames, so a 20 Hz ping to the gateway
  is the traffic source.

## Using it from another agent

The scripts have no Claude dependency. Point any coding agent at `SKILL.md`
as its instructions and let it call the scripts; the SDK README also ships a
`CLAUDE.md` with the same rules (discover modules, never invent APIs, no
`delay()` in `loop`, never edit generated files).
