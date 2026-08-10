# ARTICLE_NOTES.md — Liquid Article Source Notes

**Status:** Working notes  
**Purpose:** Raw material for a future article about Liquid, Solid, and Liquid Layer  
**Do not treat this as implementation guidance.** Operational scope lives in `AGENTS.md` and milestone status lives in `DEVELOPMENT_TRACKING.md`.

---

## Core Thesis

Liquid separates adaptive reasoning from deterministic execution.

The adaptive layer can infer context, propose behavior, generate or revise scripts, explain decisions, and negotiate preferences. The Solid runtime executes only explicit, inspectable runtime data through deterministic managers and systems.

This matters because the target environment is physical and personal. Smart-environment actions need to be predictable, auditable, and reversible enough to trust, while neurodivergent support needs flexibility around ambiguous context, changing routines, sensory load, transitions, and task initiation.

---

## Article Angle

The useful framing is not "LLM controls a smart home." The stronger claim is:

> probabilistic systems can help shape behavior, but deterministic systems should own execution, conflict handling, auditability, and replay.

Liquid Layer is the human-facing research application. Liquid is the reusable engine. Solid is the deterministic first stage that makes later adaptation safe enough to study.

---

## What M1 Now Proves

M1 proves the first technical foundation:

- behaviors exist as the domain identity instead of generic entities;
- intents are owned by behaviors and use global recyclable intent-record handles;
- components are shared named data, not behavior-owned objects;
- behaviors receive read/write access to named component instances;
- `World` is the public state boundary for behavior ownership, permissions, signatures, and cleanup, while `Coordinator` remains internal consistency logic;
- systems can track matching behavior membership through component-type signatures;
- cleanup and recycling paths are covered by assert tests, deterministic stress tests, and opt-in sanitizer builds.

This is enough to say the project now has a small Solid core, not just an architecture sketch.

M2 extends that foundation with immutable typed intent records, persistent/until-time lifetime metadata, stateless expiration checks, and Coordinator-owned cleanup when intent targets or write access become invalid.

---

## Themes To Develop Later

- Why "adaptive" and "deterministic" should be separate layers.
- Why neurodivergent-support environments need personalization without giving up predictability.
- Why intents are proposals, not immediate actions.
- Why behavior ownership matters for auditability.
- Why shared named components fit smart-environment devices better than exclusive entity-owned components.
- How replay/logging can turn adaptive smart environments into researchable systems.
- Why LLM arbitration should be exceptional rather than the default conflict-resolution path.

---

## Open Article Questions

- Which concrete scenario should introduce the article: study setup, hyperfocus interruption, sensory lighting, sleep routine, or task-start support?
- How much C++ implementation detail belongs in the article versus a separate technical appendix?
- Should the article present Liquid as a research architecture first, or as an engine-building narrative first?
- What evidence will be needed before making claims about reducing cognitive friction?
- Which diagrams are needed: layer diagram, intent lifecycle, behavior/component access, or frame flow?
