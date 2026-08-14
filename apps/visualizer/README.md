# Solid Scope

Solid Scope is the local, hardware-free browser view for the experimental Solid simulation branch. It does not implement a second runtime: the browser sends explicit inputs to a loopback-only bridge, which launches `liquid_sim_trace`; that executable drives the same `SimulationScenario`, `Runtime`, and `LuaBehaviorRunner` used by the M6 text CLI.

## Build and run

```sh
cmake -S . -B build
cmake --build build --target liquid_sim_trace
python3 apps/visualizer/server.py --trace-executable build/liquid_sim_trace
```

Open the loopback URL printed by the server. The bridge accepts only `127.0.0.1` or `::1`, serves a fixed three-file allowlist, and applies same-origin, per-process request-token, size, timeout, and child-process checks.

## Reading the instrument

- **Actual component** is the confirmed `Light.officeLight` value. It changes only when an authoritative adapter report or observation is applied — never from selection alone. Its lamp patch shows the confirmed brightness as fill luminance.
- **Command / Adapter result / Simulated device** are the external-effect lanes: the desired value dispatched, the adapter's report disposition, and the simulated device's physical truth (with its own lamp).
- **Proposed intents** are immutable requests committed by the Lua lifecycle execution.
- **Selected desire** is the resolver's winning request for that frame. Desire is not physical truth; watch it lead the actual lane by one report round-trip.
- **Runtime phase rail** replays the completed `FrameLog` phases — begin, expire, input, behavior, decision, resolve, end. It is not live intra-frame timing.
- **Script state** marks the real Lua execution boundary. Solid does not expose source-line stepping.

Playback speed, pause, and step affect only browser presentation. Runtime time always comes from the explicit nondecreasing frame-time inputs.

## Verification

```sh
ctest --test-dir build --output-on-failure
```

The test suite covers the shared scenario, deterministic `liquid.trace.v2` NDJSON, bridge validation and cleanup, the real bridge-to-runtime path, and the original M1–M6 regressions. `/?selftest=1` runs the browser's canonical full-loop and isolated-script-failure presentation checks before leaving the three-frame `10 → 70 → 30` evidence on screen. `tests/test_visualizer_selftest.js` runs the same self-test headlessly through a dependency-free DOM shim when a system Node.js is present.
