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
- `scripts/monitor.py` — bounded, cross-platform event listener
- `scripts/simulate.py` — host-side firmware simulation with scripted inputs and call expectations
- `scripts/atech_lint.py` — static checks on the `code:` block (shared by describe and simulate)
- `scripts/sim/` — fake `Arduino.h`, `esp_system.h` and the recording runtime used by the simulator

## Using it from another agent

The scripts have no Claude dependency. Point any coding agent at `SKILL.md`
as its instructions and let it call the scripts; the SDK README also ships a
`CLAUDE.md` with the same rules (discover modules, never invent APIs, no
`delay()` in `loop`, never edit generated files).
