# Solid Scope

Solid Scope is the local, hardware-free browser view for the Solid simulation
on `main`. It does not implement a second runtime: the browser sends explicit
inputs to a loopback-only bridge, which launches `liquid_sim_trace`; that
executable drives the same `SimulationScenario`, `Runtime`, and
`LuaBehaviorRunner` used by the M6 text CLI.

## Build and run

```sh
cmake -S . -B build
cmake --build build --target liquid_sim_trace
python3 apps/visualizer/server.py --trace-executable build/liquid_sim_trace
```

Open the loopback URL printed by the server. The bridge accepts only
`127.0.0.1` or `::1`, serves a fixed three-file allowlist, rejects malformed or
conflicting Origin and Content-Length framing, and applies same-origin,
per-process request-token, size, timeout, and child-process checks. On Unix it
supervises and terminates the trace executable's complete process group,
including descendants left after the leader exits.

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

Scope is an owner-operated Linux development instrument, not a supported
browser product. The bridge enforces bounded transport and trace-envelope
validation; it trusts the bundled trace executable for individual v2 event
semantics. Version 0.1.0 does not promise cross-browser compatibility beyond
the dependency-free presentation self-test and the owner's selected setup.

## Product brief

*(formerly `PRODUCT.md`, "Product — Solid Scope"; text verbatim, headings
demoted one level. The visual system is in [DESIGN.md](DESIGN.md).)*

This document describes **Solid Scope**, the owner-operated local simulation
instrument that ships beside the engine. It is not the product definition of
the Liquid engine or of the future Liquid Layer application; those live in
[`Liquid_Concepts_and_Architecture.md`](../../docs/LIQUID_CONCEPTS.md)
and the Stage 2 documents.

### Register

product (Solid Scope development instrument)

### Users

Liquid's project owner and future engine contributors use this interface while developing and validating deterministic Solid scenarios. They need to understand what the runtime actually did without reading raw logs or mistaking a selected intent for applied physical state.

### Product Purpose

Solid Scope is a local development instrument for observing one hardware-free simulation from Lua source through intent creation, frame execution, expiration, resolution, and final Runtime health. Success means the user can explain a run at a glance, replay it deterministically, and inspect bounded failures without weakening or duplicating Solid.

### Brand Personality

Precise, calm, and candid. The interface should feel like a trustworthy bench instrument: technically dense where the evidence requires it, quiet everywhere else, and explicit about the difference between live observations and recorded replay.

### Anti-references

- Generic dark developer dashboards with neon gradients, glowing glass cards, and decorative telemetry.
- SaaS analytics templates built from interchangeable metric cards.
- Fake debugger theatrics that imply source-line or intra-frame visibility the engine did not report.
- Smart-home control panels that present a desired state as if a physical device already changed.
- Overstimulating motion, color-only status, and low-contrast secondary text.

### Design Principles

1. **Evidence before spectacle.** Every visual transition must correspond to an observed event or be clearly labeled as replay.
2. **Separate fact from desire.** Actual component state, proposed intents, and selected intent always occupy distinct visual lanes.
3. **Progressive technical depth.** The causal path is legible first; exact IDs, counts, phases, and diagnostics remain available without dominating.
4. **Determinism is visible.** Explicit simulation time, sequence numbers, and replay controls reinforce that Solid is not driven by wall-clock coincidence.
5. **Failure teaches.** Empty, invalid, disconnected, rolled-back, and faulted states explain what happened and what the user can do next.

### Accessibility & Inclusion

Target WCAG 2.2 AA contrast and interaction behavior. All controls require keyboard access, visible focus, programmatic labels, and non-color state cues. Motion must respect `prefers-reduced-motion`; playback must support pause and step controls. The information hierarchy should reduce cognitive load through stable placement, plain language, bounded event density, and no unexpected animation.
