# AGENTS.md — Liquid Development Context

## Current work

Liquid Layer is the future adaptive smart-environment application. Liquid is
its reusable authoring/control layer. Solid is the deterministic C++20
foundation, released at v0.1.0.

**No Stage 2 implementation step is active.** The pre-Liquid Solid hardening
has landed on `main` (`5979e14`, follow-up `2aee8a4`), and the Liquid
documentation handoff is integrated. L0–L6 are specified design, not
implemented and not active; a step becomes active only when the owner records
its activation in [tracking](DEVELOPMENT_TRACKING.md).

Allowed without activation: project Markdown, specifications in existing
`docs/`, read-only code review, and isolated validation builds. Do not create
Stage 2 source directories, dependencies, adapters, prompts, or runtime
behavior. Preserve other worktrees and their unfinished edits. The
[validation report](docs/LIQUID_DOCUMENTATION_VALIDATION.md) records the dated
docs/code baselines of the design review.

## Reading and authority

Read [tracking](DEVELOPMENT_TRACKING.md), [the roadmap](docs/LIQUID_STAGE2_PLAN.md),
the common implementation contract, active milestone spec, and cited Solid
code/tests. All documents use this authority rule:

1. Explicit current owner instructions.
2. This file for operational scope; tracking for activation/status.
3. Focused Solid contracts for existing behavior; active milestone spec and
   common Stage 2 contract for explicitly proposed additions.
4. Stage 2 roadmap and conceptual architecture.
5. Workflow guidance, dated audits, research notes, and model opinions.

Code/tests establish what exists; contracts establish what is intended.
Disagreement is a finding to resolve explicitly. A future specification does
not silently change released behavior or authorize implementation.

## Roles and progression

For an owner-activated step, Claude implements/tests its entire allowed scope,
including substantive `.cpp` work. Codex independently reviews its diff and
evidence. This replaces the old owner-only `.cpp` convention for those steps.
The owner approves advancement and retains publication/merge authority.

Follow [the handoff workflow](docs/HERMES_MULTI_MODEL_DEVELOPMENT_GUIDE.md).
Hermes is optional. No worker self-approves. Tests alone do not activate the
next step. If code evidence disproves a decision, revise its specification
explicitly before expanding implementation scope.

## Branches and validation

- `main` is the only long-lived branch; `experiment/stage2` is retired.
- Implementation branches start from current `origin/main` once the owner
  activates a step. Record the exact base SHA.
- Never force-push or rebase published `main`.
- Commit/push/PR/merge/release require authorization for that action.
- Use isolated worktrees for concurrent unfinished tasks.
- Each implementation step gets focused red/green evidence and the strict
  full suite with `-DLIQUID_ENABLE_STRICT_WARNINGS=ON` and
  `-DLIQUID_WARNINGS_AS_ERRORS=ON`, then `ctest --output-on-failure`.
- Milestone gates cover Linux GCC/Clang, ASan/UBSan, TSan, Core coverage and
  source/installed consumers. Portability is manually dispatched before
  releases or platform changes. See the common contract for exact commands.

## Solid invariants

- Runtime alone drives frames; World is the public state boundary. Both are
  owner-thread confined. Models/transports receive copied data.
- Registries, Coordinator, WorldState, raw slots/pointers, and credentials are
  never model-facing authority. Src-private headers remain under `src/`.
- Components are value data. Behaviors own immutable intents; systems process
  components. No component inheritance or virtual behavior objects.
- Public handles are world-bound and generational, not historical identities.
- Losing selection does not destroy a live intent. Lifetimes are persistent
  or until-time; cancellation destroys. Time is monotonic milliseconds.
- Selection, command, report and authoritative observation are distinct.
  Stopping a behavior does not undo already dispatched physical commands.
- Lua uses a fresh bounded VM, fixed owner/time, copied snapshots and allowed
  proposal/cancel/watch operations. Failed bundles restore the exact intent
  transaction. Separate read/write codecs can be asymmetric.
- Metadata never grants access. Do not retain component borrows across
  structural mutation or frames. Models do not silently rewrite approved Lua.
- Solid Event Format v1 does not hold prompts or adaptive approval records.
- Proposed extensions are additive unless an approved compatibility change
  explicitly says otherwise.

## Repository and style

`CMakeLists.txt` owns the source inventory. Create folders only when the
activated milestone's allowlist needs them. `include/liquid/detail/` is not
consumer API. Current installed targets are Core, Lua and Simulation;
`Liquid::Authoring` is a future opt-in L1 addition, not an existing target.
Solid Scope stays an optional Unix-only owner-operated instrument, excluded
from installation and never a second Runtime. Its repository split is deferred.

Small tests first; minimal headers; smallest complete change; reuse Solid
primitives. No unrelated refactors. Use four-space C++ indentation and
same-line opening braces; follow sibling style without gratuitous qualifiers.

When available, use context-mode tools to process large outputs and return
relevant findings. Native editing tools write files; keep shell output short.
