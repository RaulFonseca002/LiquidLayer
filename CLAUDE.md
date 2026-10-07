# CLAUDE.md — Liquid implementation handoff

Read [AGENTS.md](AGENTS.md) first; it owns operational scope and the shared
authority rule. [Tracking](docs/DEVELOPMENT_TRACKING.md) identifies activated steps.
**No implementation step is active unless the owner activates one. Do not
start L3 automatically.** L0 (Lua schemas and capability manifests, 29
September 2026) and L1 (host-selected scope and immutable proposals, 30
September 2026) are implemented and owner-approved. L2 (isolated proposal
evaluation) was activated by the owner on 3 October 2026 and is implemented,
on `feat/liquid-l2` and was approved by the owner on 5 October 2026 (not yet merged into `main`). Solid hardening and
the L0–L6 design documents are on `main`; L3–L6 remain specified, not
implemented and inactive.

For an owner-activated implementation step:

1. Read the [roadmap](docs/LIQUID_STAGE2_PLAN.md),
   [common contract](docs/LIQUID_IMPLEMENTATION_CONTRACT.md), active spec,
   and cited Solid code/tests. Do not rely on conversation memory.
2. Confirm the exact base revision, allowed files, prerequisites and step ID.
3. Write the behavioral regression and prove the relevant failure.
4. Implement the complete bounded step, including `.cpp` logic as required.
5. Run focused tests and the strict suite; attach commands and results.
6. Hand the diff/evidence to Codex for independent review. Correct findings
   within scope. The owner approves advancement.

The [workflow](docs/HERMES_MULTI_MODEL_DEVELOPMENT_GUIDE.md) supplies templates.
Hermes is optional. Do not invoke additional workers without task authorization.
Do not expand permissions after failures, silently revise approved scripts,
or treat simulation success as proof of physical success or user suitability.
