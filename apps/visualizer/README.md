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

- **Actual component** is the observed `Light.officeLight` value. Intent selection does not mutate it.
- **Proposed intents** are immutable requests committed by the Lua execution.
- **Selected desire** is the resolver's winning request for that frame.
- **Runtime phase rail** replays the completed `FrameLog`; it is not live intra-frame timing.
- **Script state** marks the real Lua execution boundary. Solid does not expose source-line stepping.

Playback speed, pause, and step affect only browser presentation. Runtime time always comes from the explicit nondecreasing frame-time inputs.

## Verification

```sh
ctest --test-dir build --output-on-failure
```

The test suite covers the shared scenario, deterministic NDJSON, bridge validation and cleanup, the real bridge-to-runtime path, and the original M1–M6 regressions. `/?selftest=1` runs the browser's canonical success and isolated-script-failure presentation checks before leaving the successful two-frame evidence on screen.
