---
name: atech
description: Turn a plain-language hardware idea into a working Atech ESP32-S3 build — choose the modules, tell the user exactly how to assemble them on the motherboard, generate the firmware, flash the Atech board plugged into this computer, and confirm it is alive. Use when the user invokes /atech, or asks to build, program, flash, deploy, or prototype something with Atech hardware or Atech modules. Not for the Atech hosted web app or REST API.
argument-hint: <what the board should do, e.g. "green lamp when I press the button">
---

# atech — idea → assembly guide → flashed board

You drive the open-source Atech SDK (`pip` package `atech`) through the helper
scripts in this skill. The SDK is deterministic and offline: you own the
*behaviour*, it owns pin allocation, driver assembly, compiling and flashing.
Your job is to end with the board flashed and the user knowing which module
goes in which port.

The user's request is: **$ARGUMENTS**

## 0. Locate the skill scripts

Set `SKILL_DIR` to the first of these that exists (check in this order):

1. `./.claude/skills/atech` (project-local copy)
2. `~/.claude/skills/atech` (global copy)

All commands below are `bash $SKILL_DIR/scripts/<name>`. The first ever call
of `atech-env.sh` on a machine installs the SDK into `~/.atech/venv` (needs
network once, about a minute); afterwards everything runs offline. The first
ever `build` downloads the ESP32-S3 toolchain through PlatformIO (one to two
minutes); later builds take seconds.

## 1. Find the board

```bash
bash $SKILL_DIR/scripts/atech-env.sh ports
```

- Exactly one device: use it as `PORT`. **The name can change after a reset** (`/dev/ttyACM0` may come back as `/dev/ttyACM1`); rerun `ports` after any reset or flash.
- Several: pick the one whose description mentions Espressif / USB JTAG / vid 0x303a, otherwise ask the user which one.
- None: stop and tell the user to connect the board with a USB-C *data* cable. Offer `--no-upload` to build only.

Board *model* cannot be read over USB. Decide it like this:
1. the user named it ("on the 14port") → use that id;
2. else `~/.atech/default_board` exists → use its content;
3. else ask once with AskUserQuestion (options `8port`, `10port`, `14port`; the port count is printed on the PCB), then save the answer to `~/.atech/default_board`.

## 2. Discover before you design

```bash
bash $SKILL_DIR/scripts/atech-env.sh python $SKILL_DIR/scripts/catalog.py --board <board-id>
```

Read the module table, then request the full API of every module you are
considering:

```bash
bash $SKILL_DIR/scripts/atech-env.sh python $SKILL_DIR/scripts/catalog.py <id> <id> ...
```

The `usage` block printed per module is the **complete** public C++ API you may
call. Module ids, method names, event keys and action keys come only from this
output. If the idea needs hardware that is not in the catalog, say so and
propose the closest module instead of inventing one.

## 3. Write the project

Projects live in `${ATECH_PROJECTS:-~/.atech/projects}/<slug>/project.yaml`
(slug: short kebab-case from the idea). Create the directory and write:

```yaml
name: <slug>
board: <board-id>
modules:
  - {id: button,   instance: btn, port: 3}          # single-width: port
  - {id: dc_motor, instance: m1,  ports: [1, 2]}    # size=2: an adjacent pair from the board's list
code:
  setup: |
    // optional, runs once after every module's own begin()
  loop: |
    // your behaviour; runs every iteration after the module templates
```

Rules for `code:` (they come from the SDK authors):

- Only call methods shown in each module's `usage`. Instance names are the C++ variable names.
- **Module templates run before your code in `loop()` and consume one-shot flags.** If a module's template already calls `x.wasPressed()` (the button does), your own `x.wasPressed()` returns false except on contact bounce. Poll state instead (`isPressed()`, `getState()`), keep a `static bool last`, detect the edge yourself, and debounce with `millis()` (30 ms). The code check in step 4 flags this.
- No `delay()` in `loop` — use `millis()` timers so events keep flowing.
- **Displays (`st7735_tft`): never call `setRotation()` from `code:`.** The driver initialises this 160x80 panel for its tuned orientation (rotation 3 with a BGR colour override). A user-side rotation left the panel showing only the Atech boot splash forever while the firmware ran fine underneath. Draw for the default orientation; the code check flags this.
- **Every screen draws a heartbeat**: a small square that alternates colour on each redraw (e.g. `fillRect(154, 74, 6, 6, hb ? COLOR_YELLOW : 0x2104)`). The driver's boot splash persists until the first `display()`, so without a heartbeat a frozen panel is indistinguishable from a booting one.
- Modules already emit their own events; only add `Serial.println` of `{"type":"event","payload":{...}}` for custom state.
- Never place anything on a reserved port; double-width modules need one of the printed adjacent pairs.
- Never hand-edit anything under `build/` — it is regenerated every build.
- Keep it small. A few lines of behaviour is the intended shape.
- **Keep `loop()` short.** Anything heavier than a few hundred microseconds (signal processing, network sends, big buffers) belongs in its own FreeRTOS task that publishes results for `loop()` to read. A long `loop()` starves everything else and interleaves badly with a bit-banged display refresh.
- **On ESP32-S3, `ARDUINO_USB_CDC_ON_BOOT=1` (the Atech SDK default) can deadlock the boot when no USB host is attached** and the WiFi stack is linked — the board reaches setup but the first loop never runs (screen stuck on the splash/black). It is intermittent; a serial monitor attached HIDES it (a host keeps the USB-CDC stable). For a node that must run standalone, expect this and validate detached.
- **Diagnosing an intermittent boot hang:** turn the loop watchdog OFF (with it on, a hang silently reboots and looks like a pass — it invalidates every "pass"). Read a frozen board through an OFF-panel channel (paint the phase to the screen, or an RTC marker shown on the next boot) — never by opening the serial port: on the S3 that both resets the board and, as a host, masks the bug. Isolate with matched single-variable controls and clean-purged builds.
- **Purge the build tree when the module set changes.** The SDK does not clear `<project>/build/lib`, so a renamed/removed module lingers and the linker can bind the wrong object (two modules defining the same class → `undefined reference`, or silent wrong-code). `rm -rf <project>/build` before a module-set change.
- **Build in stages and gate each one with a warm reset.** Add one module or behaviour at a time; after flashing, the user presses the board's reset once and watches for 60 s: text + heartbeat alive = pass. Never open the serial port during that watch (opening it resets the board). Cold power cycles do not count as a pass: the display panel keeps power across a chip reset and only a warm reset shows the real state.

Prefer ports that are physically convenient: put a display or LED grid where
it faces the user, motors at corners (the board notes list them), sensors away
from motors.

## 4. Check the plan before flashing

```bash
bash $SKILL_DIR/scripts/atech-env.sh python $SKILL_DIR/scripts/describe_project.py <project-dir>
```

Fix any `PROBLEM:` line and every `WARNING:` under "Code check" and rerun.
Keep the printed layout; it goes in your final answer.

## 4b. Simulate before flashing (mandatory when the board has inputs)

Run the real generated firmware on this computer with a scripted scenario.
State what you expect *before* running, as `--expect` counts of mocked calls:

```bash
bash $SKILL_DIR/scripts/atech-env.sh python $SKILL_DIR/scripts/simulate.py <project-dir> \
  --scenario "100 press btn; 400 release btn; 700 press btn; 1000 release btn" --bounce 3 \
  --expect "led.setAll=1,led.clear=1,led.show=2"
```

- Scenario verbs: `press|release|tap <instance>` for button-like inputs, `high|low <instance>` for a module's signal pin, `pin <gpio> <0|1>`, `analog <gpio> <v>`, `ret <inst.method> <n>` to script what a mocked sensor returns. Times are ms. `--bounce 3` adds contact bounce to every edge; always use it for buttons.
- GPIO-only drivers (button, motors, PIR) run for real; library-backed drivers (LEDs, displays, I2C sensors) are auto-mocked and every call is recorded, so the trace shows `led.setAll(0, 255, 0)` and so on with timestamps.
- Mocked methods return 0 until you script them, so a mocked sensor looks disconnected: for sensor-driven logic add `0 ret th.isConnected 1` and e.g. `500 ret th.readTemperature 31.5` to the scenario. The PIR driver has a real 30 s warm-up, so PIR scenarios need `--duration-ms 32000` or more.
- Read the trace against the idea: right calls, right order, right times, nothing firing on release when it should fire on press, no double toggles from bounce. `--quiet` prints only the summary once you trust the trace.
- Exit 1 is a compile error in your `code:` (the message points at the user-loop section), exit 3 is an `--expect` mismatch. Fix the yaml and rerun; never flash on a failing simulation.
- Time-based behaviour: `--duration-ms` and `--tick-us` control the virtual clock; `millis()` advances one tick per `loop()`.

The simulator is not a physics model: it doesn't know colours, sound, or I2C
timing. It proves the control flow, which is where the bugs are.

## 5. Build, flash, listen

```bash
bash $SKILL_DIR/scripts/deploy.sh <project-dir> --port <PORT> --monitor 8
```

This validates, builds, uploads, then prints live events for 8 seconds. On a
`[FAIL]` read the log tail: compile errors mean your `code:` used something
not in `usage` — fix the yaml and rerun. Hints for serial permission and busy
ports are printed automatically. Do not retry uploads in a loop; two attempts,
then report.

Flashing with modules unplugged is fine for GPIO modules. I2C modules
(sensors, displays) are probed at boot, so tell the user to press the board's
Restart button after seating them if events for those modules are missing.

Serial facts that shape every observation:
- **Opening the serial port resets the board** (ESP32-S3 USB-Serial-JTAG; cannot be disabled). Every `monitor`, `send` or flash restarts the firmware and any calibration it was doing.
- To watch across resets use `monitor.py --follow` (a reconnecting logger that survives disconnects and device renames); never chain ad-hoc port opens.
- `atech send` needs a value; the SDK rejects `null`, so send `1` for value-less actions.
- If the screen shows **only the Atech logo** after boot while events still flow, the loop is not painting the panel: check for a user-side `setRotation()` first.
- The `(Restart)` and `(USB-C)` positions on the layout are fixed board hardware. A button module plugged into the Restart footprint is just a hardware reset — it cannot be read by code and every press restarts calibration. Software buttons go on a real port.

## 6. Report

Final message, in this order, no preamble:

1. **Assembly** — the layout picture from step 4 in a code block, then one bullet per module: module name, port number, orientation notes if the board notes give any.
2. **What it does** — two or three sentences describing the behaviour and how to trigger it.
3. **Tested** — one line: the scenario you simulated and that the expected calls matched (or what you changed because they didn't).
4. **Deploy result** — flashed or not, port, firmware size, and a few of the live event lines (or why there were none).
5. **Iterate** — the project path, and that saying `/atech <change>` again edits the same project when the idea is clearly the same.

Keep it short. No SDK internals in the report unless something failed.

## Guardrails

- Never flash a board the user did not plug in on purpose; if `ports` shows a device that is not an Espressif/Atech unit, ask before uploading.
- Never `pip install` outside `~/.atech/venv`; the wrapper handles installs.
- The hosted atech.dev chat/REST API is not part of this skill. Everything runs locally.
- Multiple boards, custom modules (`modules_path:`), or WiFi transport are out of scope unless the user asks; the SDK supports custom modules via a folder next to project.yaml.
