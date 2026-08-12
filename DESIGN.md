---
name: Solid Scope
description: A candid laboratory instrument for observing deterministic Solid simulations.
colors:
  trace-orange: "oklch(0.630 0.190 38.6)"
  canvas: "oklch(1.000 0.000 0)"
  instrument-surface: "oklch(0.965 0.006 250)"
  instrument-surface-strong: "oklch(0.920 0.008 250)"
  mineral-ink: "oklch(0.180 0.012 250)"
  evidence-muted: "oklch(0.450 0.020 250)"
  deterministic-cobalt: "oklch(0.520 0.190 264)"
  healthy-teal: "oklch(0.500 0.125 175)"
  bounded-red: "oklch(0.520 0.190 28)"
typography:
  interface:
    fontFamily: "system-ui, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif"
    fontSize: "1rem"
    fontWeight: 450
    lineHeight: 1.5
    letterSpacing: "-0.01em"
  heading:
    fontFamily: "system-ui, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif"
    fontSize: "1.25rem"
    fontWeight: 700
    lineHeight: 1.2
    letterSpacing: "-0.025em"
  data:
    fontFamily: "ui-monospace, SFMono-Regular, Consolas, Liberation Mono, monospace"
    fontSize: "0.8125rem"
    fontWeight: 500
    lineHeight: 1.55
rounded:
  control: "6px"
  surface: "10px"
  pill: "999px"
spacing:
  xs: "4px"
  sm: "8px"
  md: "12px"
  lg: "16px"
  xl: "24px"
  xxl: "32px"
components:
  button-primary:
    backgroundColor: "{colors.mineral-ink}"
    textColor: "{colors.canvas}"
    rounded: "{rounded.control}"
    padding: "10px 16px"
  button-primary-hover:
    backgroundColor: "{colors.deterministic-cobalt}"
    textColor: "{colors.canvas}"
    rounded: "{rounded.control}"
    padding: "10px 16px"
  input:
    backgroundColor: "{colors.canvas}"
    textColor: "{colors.mineral-ink}"
    rounded: "{rounded.control}"
    padding: "9px 11px"
---

# Design System: Solid Scope

## 1. Overview

**Creative North Star: "The Transparent Bench Instrument"**

Solid Scope looks like a tool laid out for careful inspection in daylight: a neutral work surface, dark structural ink, and a small number of calibrated signals. Density is earned by causal evidence, not by dashboard convention. The runtime rail is the dominant object because execution order is the product's central fact.

The system rejects generic dark developer dashboards, SaaS metric-card grids, glowing telemetry, and fake debugger theatrics. Live boundary events and recorded phase replay must be visibly different. Actual component state, proposed intents, and selected desire never share a lane.

**Key Characteristics:**

- Daylight-neutral, mostly flat surfaces.
- One structural runtime rail rather than a field of cards.
- Warm orange for execution, cobalt for selection, teal for health, and red for bounded failure.
- Human-readable interface type paired with compact monospace evidence.
- Stable placement and restrained motion for cognitively calm inspection.

## 2. Colors

The palette behaves like calibrated indicator lamps on a neutral instrument, with semantic color occupying less than ten percent of the screen.

### Primary

- **Trace Orange** (`oklch(0.630 0.190 38.6)`): the active execution pulse, current script interval, and playhead. It never decorates inactive surfaces.

### Secondary

- **Deterministic Cobalt** (`oklch(0.520 0.190 264)`): selected intent, keyboard focus, and explicit user-controlled state.

### Tertiary

- **Healthy Teal** (`oklch(0.500 0.125 175)`): successful completion and healthy Runtime state.
- **Bounded Red** (`oklch(0.520 0.190 28)`): script errors, transport failures, and faulted Runtime state.

### Neutral

- **Canvas** (`oklch(1.000 0.000 0)`): the application background and code field.
- **Instrument Surface** (`oklch(0.965 0.006 250)`): grouped controls and evidence lanes.
- **Instrument Surface Strong** (`oklch(0.920 0.008 250)`): disabled controls, dividers, and recorded replay states.
- **Mineral Ink** (`oklch(0.180 0.012 250)`): primary text, code, and structural rules.
- **Evidence Muted** (`oklch(0.450 0.020 250)`): secondary labels that still meet AA contrast.

**The Signal Budget Rule.** Semantic color occupies no more than ten percent of a resting screen. If the interface looks colorful before a run starts, the signals have lost meaning.

## 3. Typography

**Display Font:** System UI with platform-native fallbacks

**Body Font:** System UI with platform-native fallbacks

**Label/Mono Font:** UI Monospace with SF Mono, Consolas, and Liberation Mono fallbacks

**Character:** The interface family is quiet and immediately familiar. Monospace is reserved for evidence—Lua, IDs, frame times, values, and diagnostics—so technical content changes texture without becoming ornamental.

### Hierarchy

- **Heading** (700, 1.25rem, 1.2): section and instrument titles.
- **Title** (650, 1rem, 1.3): lane titles and active frame summaries.
- **Body** (450, 1rem, 1.5): instructions and explanations, capped at 70ch.
- **Data** (500, 0.8125rem, 1.55): code, events, values, and timestamps.
- **Label** (650, 0.75rem, 0.01em): compact control and status labels; sentence case by default.

**The Evidence Typeface Rule.** Monospace communicates machine evidence only. Buttons, navigation, headings, and explanatory prose remain in the interface family.

## 4. Elevation

Solid Scope is flat by default. Depth comes from tonal layering and structural rules, not floating cards. A compact shadow may appear only on a focused popover or sticky control that truly leaves the document plane.

**The Bench Plane Rule.** If a resting panel needs a wide shadow to look grouped, its hierarchy is wrong. Fix spacing and tonal contrast instead.

## 5. Components

### Buttons

- **Shape:** compact and precise (6px radius).
- **Primary:** Mineral Ink with Canvas text and 10px × 16px padding.
- **Hover / Focus:** cobalt hover; a 2px cobalt focus outline with 2px offset; active state translates by at most 1px.
- **Secondary:** Canvas with a single Mineral Ink structural border and no shadow.

### Chips

- **Style:** compact semantic label with an icon or text cue in addition to color.
- **State:** inactive chips are neutral; orange means executing, cobalt selected, teal complete, red failed.

### Cards / Containers

- **Corner Style:** restrained grouping (10px radius maximum).
- **Background:** Canvas or Instrument Surface.
- **Shadow Strategy:** none at rest.
- **Border:** one subtle structural rule when adjacent surfaces share a tone.
- **Internal Padding:** 16px or 24px, never arbitrary in-between values.

### Inputs / Fields

- **Style:** Canvas field, 6px radius, dark text, and a structural neutral border.
- **Focus:** cobalt outline independent of border color.
- **Error / Disabled:** bounded-red message plus `aria-invalid`; disabled uses Instrument Surface Strong and remains legible.

### Navigation

The header is a status bar, not site navigation. It keeps the product name, connection state, simulation time, and primary run action on one stable line before collapsing deliberately on narrow screens.

### Runtime Rail

The five ordered phases form one connected horizontal instrument. Orange marks current execution, neutral marks recorded completion, and cobalt links the resolved selection to its evidence lane. On narrow screens the rail becomes a vertical ordered sequence rather than shrinking labels.

## 6. Do's and Don'ts

### Do:

- **Do** keep actual component state, proposed intents, and selected desire in separate labeled lanes.
- **Do** label replayed `FrameLog` phases as recorded evidence rather than live internal timing.
- **Do** use exact frame time and event sequence as the stable orientation system.
- **Do** pair every semantic color with text, shape, or icon state.
- **Do** respect reduced motion with instant state changes and fully functional pause/step controls.

### Don't:

- **Don't** build a generic dark developer dashboard with neon gradients, glowing glass cards, or decorative telemetry.
- **Don't** use a SaaS analytics template made of interchangeable metric cards.
- **Don't** imply source-line or intra-frame visibility the engine did not report.
- **Don't** present a selected desired brightness as if the physical component already changed.
- **Don't** use side-stripe accents, gradient text, oversized rounded panels, low-contrast secondary text, or color-only status.
