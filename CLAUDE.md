# CLAUDE.md — Liquid Development Context (Claude Code)

**Read `AGENTS.md` in this folder first and follow it exactly.** It is the single authoritative operational context for all coding agents in this repository (project snapshot, current milestone, branch workflow, Solid core rules, coding role, development style). It is deliberately kept as one source of truth and is updated at every milestone transition — do not duplicate its content here, and if this file and `AGENTS.md` ever disagree, `AGENTS.md` wins.

Documentation authority order (from the accepted completion audit):

1. `AGENTS.md` — operational scope and branch ownership
2. `DEVELOPMENT_TRACKING.md` — approved milestone status and order
3. `COMPLETE_SOLID.md` — accepted Solid completion audit and gates
4. `Liquid_Concepts_and_Architecture.md` — canonical architecture; section 13 is the canonical Lua script-authoring contract
5. `CURRENT_STATE_EVALUATION.md`, `M6_TEST_BASE.md`, `ARTICLE_NOTES.md` — historical / non-normative evidence

Claude-specific reminders:

- The project owner implements core `.cpp` logic unless they explicitly ask otherwise. Default to generating headers, tests, CMake, boilerplate, compile fixes, and documentation updates. Never silently implement large runtime behavior.
- Before changing code, run `git branch --show-current` and follow the single-branch workflow in `AGENTS.md`: all work starts on a short-lived branch from `origin/main` and lands on `main` with the strict suite and the remote CI matrix green. `experiment/stage2` is retired; engine/tool separation is the CMake packaging boundary, not a branch.
- Baby steps: small failing test first, smallest complete change, then the strict full suite (`-DLIQUID_ENABLE_STRICT_WARNINGS=ON -DLIQUID_WARNINGS_AS_ERRORS=ON`, `ctest --output-on-failure`).
- Do not add dependencies (no Playwright, Node, frameworks). The visualizer is a dev instrument tested manually and with dependency-free Python/JS tests.
- Do not push, open PRs, or merge shared branches without the owner's explicit approval.
