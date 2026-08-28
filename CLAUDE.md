# CLAUDE.md — Liquid Development Context (Claude Code)

**Read `AGENTS.md` in this folder first and follow it exactly.** It is the authoritative operational context for coding agents in this repository. Do not duplicate the active milestone here; if this file and `AGENTS.md` disagree, `AGENTS.md` wins.

Documentation authority order:

1. `AGENTS.md` — active milestone, allowed scope, branch/coding-agent rules
2. `DEVELOPMENT_TRACKING.md` — approved milestone status, order, and evidence
3. `docs/LIQUID_STAGE2_PLAN.md` — Stage 2 architecture, roadmap, research, and deferred decisions
4. `docs/LIQUID_L0_IMPLEMENTATION_SPEC.md` — exact current L0 semantics and test expectations
5. `COMPLETE_SOLID.md` — accepted Solid v0.1 completion audit and gates
6. focused `docs/*.md` contracts — public API, lifecycle scripting, threading, effects, events, replay, security/support
7. `Liquid_Concepts_and_Architecture.md` — conceptual Solid/Liquid/Liquid Layer architecture and vocabulary
8. dated evaluations/article/test-base notes — historical/non-normative evidence

The executable Lua lifecycle contract is `docs/LIFECYCLE_SCRIPTING.md` plus the current public scripting headers. Do not rely on an old section number from `Liquid_Concepts_and_Architecture.md`.

Claude-specific reminders:

- Current milestone is L0 — Model-Facing Lua Capability Contract. Do not implement provisional L1-L6 work unless `AGENTS.md` is updated to approve it.
- Read `docs/LIQUID_L0_IMPLEMENTATION_SPEC.md` before drafting the L0 headers/tests. In particular, keep `readSchema` and `writeSchema` separate because Lua codec `encode`/`decode` may be asymmetric.
- The project owner implements substantive core `.cpp` logic unless they explicitly ask otherwise. Default to headers, tests, CMake, small boilerplate, compile fixes, and documentation. Never silently implement large runtime behavior.
- Before changing implementation code, run `git branch --show-current` and follow the workflow in `AGENTS.md`: short-lived branch from current `origin/main`, smallest complete change, strict suite before merge.
- Baby steps: tests first, minimal public shape, then implementation. Prefer existing Solid primitives over new abstractions.
- Do not add model/provider, Hermes, MCP, HTTP, prompt-orchestration, hardware, or other dependencies in L0.
- Generated/model-facing data never gets direct `World`, registry, slot, pointer, or cross-thread Runtime authority.
- Do not push, open PRs, or merge shared branches without the owner's explicit approval for that action.
