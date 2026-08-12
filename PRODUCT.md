# Product

## Register

product

## Users

Liquid's project owner and future engine contributors use this interface while developing and validating deterministic Solid scenarios. They need to understand what the runtime actually did without reading raw logs or mistaking a selected intent for applied physical state.

## Product Purpose

Solid Scope is a local development instrument for observing one hardware-free simulation from Lua source through intent creation, frame execution, expiration, resolution, and final Runtime health. Success means the user can explain a run at a glance, replay it deterministically, and inspect bounded failures without weakening or duplicating Solid.

## Brand Personality

Precise, calm, and candid. The interface should feel like a trustworthy bench instrument: technically dense where the evidence requires it, quiet everywhere else, and explicit about the difference between live observations and recorded replay.

## Anti-references

- Generic dark developer dashboards with neon gradients, glowing glass cards, and decorative telemetry.
- SaaS analytics templates built from interchangeable metric cards.
- Fake debugger theatrics that imply source-line or intra-frame visibility the engine did not report.
- Smart-home control panels that present a desired state as if a physical device already changed.
- Overstimulating motion, color-only status, and low-contrast secondary text.

## Design Principles

1. **Evidence before spectacle.** Every visual transition must correspond to an observed event or be clearly labeled as replay.
2. **Separate fact from desire.** Actual component state, proposed intents, and selected intent always occupy distinct visual lanes.
3. **Progressive technical depth.** The causal path is legible first; exact IDs, counts, phases, and diagnostics remain available without dominating.
4. **Determinism is visible.** Explicit simulation time, sequence numbers, and replay controls reinforce that Solid is not driven by wall-clock coincidence.
5. **Failure teaches.** Empty, invalid, disconnected, rolled-back, and faulted states explain what happened and what the user can do next.

## Accessibility & Inclusion

Target WCAG 2.2 AA contrast and interaction behavior. All controls require keyboard access, visible focus, programmatic labels, and non-color state cues. Motion must respect `prefers-reduced-motion`; playback must support pause and step controls. The information hierarchy should reduce cognitive load through stable placement, plain language, bounded event density, and no unexpected animation.
