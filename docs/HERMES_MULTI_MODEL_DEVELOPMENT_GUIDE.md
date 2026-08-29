# Hermes Multi-Model Development Guide

**Status:** Development-process guide; does not change Liquid architecture or milestone scope  
**Research date:** 29 August 2026  
**Primary models:** Claude Fable 5 and GPT-5.6 Sol  
**Coordinator:** Hermes Agent  
**Repository:** `RaulFonseca002/tcc`

---

## 1. Purpose

This document defines how to use **Hermes Agent as the local development coordinator** for Liquid while delegating work to two different frontier coding systems:

- **Claude Fable 5**, normally through the native **Claude Code** CLI;
- **GPT-5.6 Sol**, normally through the native **Codex** CLI.

The goal is not to make two models repeatedly debate each other. The goal is to exploit genuinely different strengths while keeping development bounded, testable, auditable, and under the repository owner's control.

The intended relationship is:

```text
                         HUMAN OWNER
                 scope / architecture / merge gate
                              |
                              v
                         HERMES AGENT
                coordinator / router / task state
                evidence collection / loop limits
                   /                      \
                  /                        \
                 v                          v
       CLAUDE CODE + FABLE 5        CODEX + GPT-5.6 SOL
        architecture / review       implementation / proof
        semantic judgment           repo/tool-heavy execution
                 \                          /
                  \                        /
                   +------ Git / tests ----+
                              |
                              v
                         LIQUID REPO
```

Hermes is **not** a new project authority, a replacement for `AGENTS.md`, or a component of Liquid itself. It is optional owner-operated development tooling.

---

## 2. Authority order

Every agent, including Hermes, follows this order:

1. explicit instruction from the project owner;
2. `AGENTS.md`;
3. `DEVELOPMENT_TRACKING.md`;
4. the currently approved milestone specification;
5. frozen Solid/public runtime contracts;
6. reproducible repository evidence: code, tests, compiler output, runtime output;
7. this orchestration guide;
8. model preference or reviewer opinion.

If a model recommendation conflicts with a higher item, the recommendation loses.

For current Stage 2 work, also read:

- `docs/LIQUID_STAGE2_PLAN.md`;
- `docs/LIQUID_L0_IMPLEMENTATION_SPEC.md`;
- `docs/LIFECYCLE_SCRIPTING.md` when the Lua execution contract is relevant.

### Non-negotiable consequence

The orchestration system never widens the current coding role. At the time this guide was written, the owner normally implements substantive core `.cpp` logic unless explicitly delegating it. Hermes may delegate headers, tests, CMake, small boilerplate, compile fixes, documentation, research, and review according to `AGENTS.md`; it must not silently convert a planning task into permission to implement core runtime logic.

---

## 3. Why Hermes is a good coordinator for this setup

Current Hermes already contains most of the infrastructure we would otherwise have to build:

- a bundled `claude-code` skill for delegating to the Claude Code CLI;
- a bundled `codex` skill for delegating to the Codex CLI;
- a skills system based on focused `SKILL.md` procedures;
- optional subagent delegation with bounded depth and optional Git worktree isolation;
- a durable Kanban system with dependencies, comments, review states, per-task skills, per-task model overrides, and Git worktree workspaces;
- separate profiles with their own model/tool configuration;
- built-in `plan`, `test-driven-development`, and `requesting-code-review` skills;
- an optional `subagent-driven-development` skill using fresh workers and independent review.

This means Liquid should **not** begin by writing a custom orchestrator plugin or a custom multi-agent framework.

Start with Hermes' existing surfaces and add project-specific skills only when repeated work demonstrates that they save effort or prevent real mistakes.

---

## 4. Recommended operating mode

### Mode A — native coding CLIs behind Hermes — **recommended first**

For actual repository work, Hermes should invoke the model through the coding harness designed for that model:

```text
Hermes
  |
  +-- claude-code skill --> claude -p ... --> Claude Code --> Fable 5
  |
  +-- codex skill -------> codex exec ... --> Codex ------> GPT-5.6 Sol
```

Why this is the default:

- Fable keeps Claude Code's repository/tool/permission behavior;
- Sol keeps Codex's terminal-centric coding harness and sandbox;
- existing Claude/Codex subscriptions or OAuth sessions can be reused where supported;
- the two workers remain independently replaceable;
- Hermes only needs to understand the task and handoff protocol, not emulate either coding runtime.

### Mode B — Hermes-native model routing

Hermes can also call models directly through its provider system and can route Kanban tasks to different model/provider combinations.

Use this mode for:

- research;
- bounded analysis;
- classification/triage;
- central API billing;
- jobs that do not benefit from a native coding CLI;
- later experiments comparing native CLI harnesses with direct inference.

Do not assume `delegate_task` alone is sufficient for Fable/Sol routing: Hermes currently configures one delegation model globally for `delegate_task`; per-task model choice belongs to Kanban or to explicit external CLI dispatch.

### Do not build Mode C yet

Do **not** write a custom Hermes plugin that recursively calls Claude and Codex until Mode A/B reveal a concrete missing capability. Existing Hermes skills, terminal execution, worktrees, profiles and Kanban already cover the first implementation.

---

## 5. Model roles — starting hypothesis

This routing table is the default for the beginning of L0. It is intentionally an **empirical hypothesis**, not a permanent statement about model rankings.

| Work type | Primary lane | Secondary lane |
|---|---|---|
| architecture / boundary decision | Fable | blind Sol second opinion when consequential |
| ambiguous requirement analysis | Fable | Sol feasibility check |
| API semantics / ownership / authority | Fable | Sol checks against concrete call sites |
| scope-creep / premature abstraction review | Fable | — |
| task decomposition / task packet | Fable or Hermes using approved plan | Sol feasibility audit |
| repository exploration | Sol | Fable only for conceptual ambiguity |
| tests / fixtures | Sol | Fable reviews missing semantic cases |
| public header scaffolding | Sol | Fable semantic review |
| CMake / package wiring | Sol | — unless architecture changes |
| compile / test / sanitizer loop | Sol | — |
| difficult terminal/debug loop | Sol | Fable if root cause appears conceptual |
| implementation diff review | Fable fresh context | Sol can run mechanical/UB/build checks |
| docs synchronization | Sol | Fable if text changes architecture meaning |
| milestone gate | Fable + Sol independently | human decides |
| web research / evidence gathering | Sol or Hermes | Fable challenges conclusions when high-impact |

### Why Fable starts in the judgment lane

Current Anthropic guidance positions Fable 5 for difficult, ambiguous and long-horizon work. Its prompting guidance also warns that at higher effort it may gather more context and deliberate beyond what routine tasks need. For this project that makes it valuable at the points where semantic judgment has high leverage, while explicit scope guards prevent it from redesigning surrounding systems.

Default Fable effort:

```text
medium  routine bounded analysis
high    architecture/review default
xhigh   milestone transitions or unusually difficult conceptual failures
```

Do not use maximum effort merely because it exists. Increase effort only when project evals show measurable value.

### Why Sol starts in the execution lane

Current GPT-5.6 guidance emphasizes strong tool/coding performance and recommends deliberately selecting reasoning effort rather than automatically maximizing it. Sol is substantially cheaper per token than Fable at current published API prices and performs strongly in terminal/coding-agent evaluations, making it a natural default for the large number of tokens consumed during repository exploration, edit/build/test/fix cycles and mechanical integration.

Default Sol effort:

```text
low/medium  mechanical or well-specified work
medium      normal implementation default
high        difficult implementation/debugging
xhigh       stuck-task diagnosis after evidence exists
max         only after project evals demonstrate an advantage
```

### Important warning

Do not reduce this to:

```text
Fable thinks; Sol types.
```

Both are frontier models and both can discover mistakes in the other's work. Routing is about expected advantage and token allocation, not artificial capability restrictions.

---

## 6. Human, Hermes, Fable and Sol responsibilities

### Human owner

The owner:

- defines or approves milestone scope;
- resolves architectural disagreement;
- decides whether substantive core implementation is delegated;
- approves widening allowed files/authority;
- remains the final merge/release gate.

No model vote overrides this.

### Hermes

Hermes should:

- classify the task;
- load the relevant project contracts;
- choose a lane;
- create/freeze a small task packet;
- create or select an isolated workspace for writing tasks;
- dispatch workers;
- preserve task state and evidence;
- ensure reviewers receive the real diff, not just implementer prose;
- enforce review-loop limits;
- escalate unresolved conflicts to the human.

Hermes should normally **not**:

- invent new architecture while routing;
- silently edit the implementation itself after delegating it;
- declare a milestone complete from worker claims alone;
- merge/push/release without explicit owner permission;
- use agent memory as proof of repository state.

### Fable lane

Fable normally works read-only and receives:

- the frozen task packet;
- relevant contracts/files;
- the actual candidate diff for review;
- proof output only when necessary.

Its job is to identify concrete semantic, contract, scope, correctness, ownership, authority and design problems.

It should **not** add optional cleanups, future-proofing or abstractions unrelated to the packet.

### Sol lane

Sol normally receives a writable isolated worktree for implementation tasks and:

- reads the packet and referenced source;
- writes tests first where appropriate;
- makes the smallest allowed change;
- compiles/runs focused tests;
- returns a diff plus proof results;
- does not self-approve.

If the packet requires a design choice not already resolved, Sol should stop and report the ambiguity rather than silently establish new architecture.

---

## 7. The canonical development loop

### Step 0 — preflight

Hermes verifies:

```text
correct repository
clean or intentionally dirty base
current branch/ref
current milestone
AGENTS.md read
relevant milestone spec read
requested authority understood
```

For a write task, never start from an unknown dirty checkout.

### Step 1 — classify

Use one of these task classes:

```text
ARCHITECTURE
IMPLEMENTATION
DEBUG
REVIEW
RESEARCH
DOC_SYNC
MILESTONE_GATE
```

Routing should be explicit in the task record.

### Step 2 — obtain judgment only when necessary

For architecture-sensitive work, ask Fable for a read-only proposal or task brief.

For a small mechanical change already fully specified, skip the Fable planning call. Do not spend frontier-review tokens simply to restate an approved packet.

### Step 3 — Sol feasibility check

Before a non-trivial implementation, Sol may inspect the actual repository and answer only:

```text
Does the task packet match the code that exists?
Are the referenced files/call sites real?
Does an existing primitive make a proposed abstraction unnecessary?
Are acceptance tests mechanically possible?
Is a required design decision still unresolved?
```

This is not permission to redesign the task.

### Step 4 — freeze the Task Packet

Once architectural decisions are sufficient, Hermes freezes the packet. An implementation worker must not receive a moving target.

The packet should be **decision-complete, not source-complete**: cite file paths, symbols and commits instead of pasting the repository into the prompt.

### Step 5 — isolated implementation

Sol executes in a dedicated Git worktree/branch.

Parallel workers must not edit the same files unless explicitly coordinated. Package-manager/lockfile or generated-output tasks that collide should be serialized.

### Step 6 — machine proof

The implementer runs the exact focused proof from the packet.

Examples for Liquid include:

```text
focused Catch2 executable
strict warning build
ctest target/full suite
Core-only consumer gate
sanitizer gate when relevant
```

Worker prose such as "all tests pass" is a claim. Test output and the resulting diff are evidence.

### Step 7 — fresh independent review

Fable reviews the **actual diff** against:

1. the frozen packet;
2. current repository contracts;
3. machine proof.

The implementer should not be used as the sole reviewer of its own work.

### Step 8 — bounded correction

Sol receives only concrete blocking/major findings and makes the smallest fixes.

Then run proof and one fresh review pass.

Default review budget:

```text
initial review
+ up to 2 correction/re-review rounds
```

A third correction round requires genuinely new blocking evidence or explicit human approval. Never run "review until both models stop finding something"; open-ended adversarial loops eventually create low-value or speculative findings.

### Step 9 — escalate disagreement

If Fable and Sol disagree after evidence has been gathered, Hermes creates a short conflict report:

```text
question
each position
direct source/code evidence
repro/test evidence
smallest consequences of each choice
recommended human decision point
```

The human resolves it.

### Step 10 — landing gate

Before merge:

- required tests/gates are green;
- diff matches allowed scope;
- no unexpected generated files or unrelated cleanup;
- docs/tracking updated if the milestone requires them;
- human approves landing.

---

## 8. Task Packet template

Use this for non-trivial delegated work.

```markdown
# Task Packet: <ID> — <title>

## Type
IMPLEMENTATION | ARCHITECTURE | DEBUG | REVIEW | RESEARCH | DOC_SYNC | MILESTONE_GATE

## Base
Repository: RaulFonseca002/tcc
Base ref/commit: <exact ref or SHA>
Milestone: <current approved milestone>

## Objective
<one observable outcome>

## Required context
- AGENTS.md
- <specific file/symbol>
- <specific spec section>

## Frozen decisions / invariants
- <decision already made>
- <contract that must not move>

## Allowed files
- <paths>

## Forbidden / non-goals
- <explicit exclusions>

## Authority
- read-only | tests/headers/CMake | workspace-write | explicitly delegated core implementation
- no push/merge/release unless separately authorized

## Acceptance criteria
- [ ] <observable criterion>
- [ ] <observable criterion>

## Proof commands
```sh
<exact focused commands>
```

## Expected handoff
Return:
1. changed files / commit or diff;
2. proof command results;
3. unresolved assumptions;
4. no extra improvements outside scope.
```

### Packet rules

A good packet:

- resolves design decisions before implementation;
- has exact acceptance criteria;
- names proof commands;
- names non-goals;
- keeps source references by path/symbol;
- does not ask the executor to "use best judgment" for an architectural boundary.

If a task cannot be expressed this way, it is probably still an architecture/research task.

---

## 9. Review output contract

Review should return structured findings, not an essay and not a vague `LGTM`.

Recommended conceptual schema:

```json
{
  "summary": "...",
  "blocking_findings": true,
  "findings": [
    {
      "severity": "BLOCKER|MAJOR|MINOR|NOTE",
      "category": "contract|correctness|scope|tests|threading|ownership|api|build|docs|security",
      "location": "file:line or symbol",
      "claim": "specific problem",
      "evidence": "why the current diff/code proves it",
      "violated_contract": "packet/spec/repo rule",
      "minimal_fix": "smallest acceptable correction",
      "required_proof": "test/check needed after correction",
      "confidence": "high|medium|low"
    }
  ],
  "uncertainties": []
}
```

Rules:

- `BLOCKER`: cannot land safely/correctly.
- `MAJOR`: material requirement or regression; should be fixed before landing.
- `MINOR`: real but non-blocking improvement; do not automatically expand scope.
- `NOTE`: observation only.
- A finding needs a location and evidence. "Could maybe be cleaner" is not a finding.
- Reviewers do not invent new requirements.
- A low-confidence speculative issue does not justify an endless repair loop.

---

## 10. Recommended Hermes/CLI setup

### Preflight tools

Verify the local environment before trusting automation:

```sh
hermes skills list
claude --version
claude auth status --text
codex --version
git --version
```

Hermes currently ships both `claude-code` and `codex` skills by default. Confirm they are present instead of installing duplicate custom wrappers.

### Fable through Claude Code

Hermes' bundled Claude Code skill recommends non-interactive print mode for automation.

A read-only planning/review invocation should conceptually resemble:

```sh
claude -p "<task packet or review request>" \
  --model claude-fable-5 \
  --effort high \
  --permission-mode plan \
  --output-format json \
  --max-turns 8
```

For stronger machine parsing, use Claude Code's `--json-schema` and read its structured result.

For review, prefer a disposable/read-only worktree or a worktree containing exactly the candidate commit. Do not give Fable write permission merely because the CLI can edit.

Avoid normalizing `--dangerously-skip-permissions` into the default development workflow.

### Sol through Codex

Hermes' bundled Codex skill recommends `codex exec` and states that Codex should be launched through a PTY from Hermes.

Implementation concept:

```sh
codex exec --sandbox workspace-write "<frozen task packet>"
```

Run it with:

```text
workdir = isolated task worktree
pty = true
background = true for long tasks
```

Pin GPT-5.6 Sol and its reasoning effort in the Codex configuration/profile that the worker uses, and verify the installed CLI resolves the intended model before relying on it.

#### Gateway/sandbox caveat

Hermes documents that Codex `workspace-write` sandboxing can fail when the CLI is launched from some Hermes gateway/service contexts because of host user-namespace/bubblewrap restrictions.

Do not silently respond by granting unrestricted machine authority.

Preferred response order:

1. run Hermes interactively/local where `workspace-write` works;
2. use a dedicated OS/container/worktree boundary;
3. verify explicit `workdir`, clean Git base and narrow packet;
4. only if the operator deliberately accepts the reduced sandbox, use a no-sandbox Codex mode;
5. always review diff and proof before landing.

`danger-full-access` or equivalent is a host-level operational choice, not a normal project permission.

---

## 11. Worktree policy

Use worktrees as the default isolation primitive for model-written code.

Properties we want:

```text
one task -> one branch/worktree
worker cannot dirty owner's checkout
parallel workers cannot overwrite each other
review sees a stable diff
failed experiments are disposable
```

Hermes supports worktree isolation in subagent delegation and `worktree` workspaces in Kanban. Claude Code also has native worktree support, while the Hermes Codex skill documents standard Git worktree use.

The repository still follows its normal branch policy: implementation work begins from current `main` on a short-lived branch, not from stale experimental branches.

### No simultaneous authority on one worktree

Do not let Fable and Sol both edit the same worktree at the same time.

If both models independently implement a solution for an eval, give them independent worktrees from the same base commit.

---

## 12. When to use Hermes Kanban

Start without Kanban if the owner is running one task at a time interactively. The conversational coordinator + native CLI skills are simpler.

Move to Kanban when at least one of these becomes true:

- multiple independent tasks are active;
- work runs while the owner is away;
- dependencies/handoffs are becoming easy to lose;
- the same workflow repeats often;
- a durable audit trail of model tasks is useful.

Kanban provides:

- persistent task states;
- dependencies;
- comments as handoff protocol;
- isolated boards;
- worktree workspaces;
- per-task skills;
- per-task model/provider override for Hermes-native workers;
- bounded retries/circuit breakers;
- separate review phase.

### Example board initialization

```sh
hermes kanban boards create liquid \
  --name "Liquid Development" \
  --description "Owner-controlled Liquid Stage 2 development" \
  --switch

hermes gateway start
```

For a coding card:

```sh
hermes kanban create "L0: draft LuaValueSchema public tests/header" \
  --assignee <configured-worker-profile> \
  --workspace worktree \
  --branch <short-lived-task-branch> \
  --skill liquid-test-first \
  --max-retries 2
```

This command is a pattern: profile names, installed custom skills and branch names must exist on the local installation.

### Important routing distinction

`delegate_task` has a single globally configured child model. Therefore:

- use it for homogeneous child batches;
- use Kanban per-task model override for heterogeneous Hermes-native model cards;
- or, for our recommended coding setup, let the parent Hermes session explicitly invoke Claude Code or Codex via their bundled skills.

---

## 13. Custom Liquid skills — proposed library

Do not implement all of these merely because they are listed. The first L0 tasks should tell us which procedures recur enough to deserve a skill.

The proposed portable skill set is:

### `liquid-contract-auditor`

**Purpose:** read-only architectural/contract review.

Should know:

- `AGENTS.md` authority order;
- Solid runtime ownership/threading;
- desire != selected != command != report != observed truth;
- losing intent remains alive;
- Lua is the generated executable boundary;
- owner/capability isolation;
- current milestone boundaries.

Output only concrete findings with source locations. No redesign unless asked.

### `liquid-task-brief`

**Purpose:** turn an approved objective into a frozen Task Packet.

Must force:

- exact objective;
- base ref;
- context paths/symbols;
- frozen decisions;
- allowed files;
- non-goals;
- authority level;
- acceptance criteria;
- proof commands;
- expected handoff.

Reject a packet that still delegates an unresolved architectural choice to the implementer.

### `liquid-test-first`

**Purpose:** enforce the repository's implementation discipline.

For L0 it should remind the worker:

- tests/minimal public header before substantive implementation;
- owner retains substantive `.cpp` work unless explicitly delegated;
- no speculative folders/targets/dependencies;
- exact focused test first;
- strict build/full suite before landing;
- no unrelated cleanup.

This can compose with Hermes' bundled `test-driven-development` skill instead of duplicating generic TDD instructions.

### `liquid-diff-review`

**Purpose:** independent fresh-context diff review.

Inputs:

- frozen packet;
- base/candidate refs;
- actual diff;
- relevant proof.

Checks:

1. spec compliance;
2. Solid/Liquid contract compliance;
3. scope creep;
4. correctness/test weakness;
5. ownership/threading/evidence mistakes;
6. API/package compatibility.

Uses the structured finding contract in this document.

This should compose with Hermes' bundled `requesting-code-review` rather than replace its general review mechanics.

### `liquid-milestone-gate`

**Purpose:** assemble evidence for a human milestone decision.

Checks:

- all approved exit criteria;
- tests/gates;
- docs/tracking synchronization;
- deferred items did not silently enter scope;
- evidence is reproducible;
- next milestone remains provisional until explicitly approved.

It may return `READY_FOR_OWNER_REVIEW` or `NOT_READY` with evidence. It cannot approve/advance the milestone itself.

### `liquid-hermes-orchestrator` — only after the workflow stabilizes

A small meta-skill can eventually encode the routing table and loop limits from this guide.

Do not copy every project contract into it. It should point to authoritative repo files and orchestrate the phase-specific skills.

---

## 14. Use existing Hermes skills instead of duplicating them

Current Hermes already provides useful generic procedures:

```text
plan
claude-code
codex
test-driven-development
requesting-code-review
```

Optional:

```text
subagent-driven-development
```

The optional subagent-driven skill explicitly uses a fresh implementer plus two review stages: spec compliance first, code quality second. This is a useful methodological reference, but our dual-model workflow can be cheaper:

```text
Fable: semantic/spec review
Sol: implementation + machine proof
Hermes built-in review/TDD: generic checklist support
```

Do not blindly run three reviewers for every three-line change.

Hermes skill bundles can group frequently co-used skills, but avoid loading every development skill into every task. Progressive loading preserves context and reduces contradictory procedural instructions.

---

## 15. Bootstrap instruction for the Hermes coordinator

The owner may give Hermes a short persistent/project instruction equivalent to:

```text
You coordinate development of RaulFonseca002/tcc.

Before acting, read AGENTS.md and the currently applicable milestone docs.
Those repository contracts outrank this instruction.

Default routing:
- Claude Fable 5 via Claude Code: architecture, ambiguity, semantic/API review,
  scope review, difficult conceptual diagnosis, milestone review.
- GPT-5.6 Sol via Codex: repository exploration, tests, headers, CMake,
  compile/test/fix loops, mechanical integration and implementation that the
  owner has explicitly authorized.

For real code edits, use an isolated Git worktree. Never let both workers edit
one worktree simultaneously. Freeze a task packet before implementation.

Treat worker summaries as claims. Treat source, diff, compiler/test output and
runtime evidence as proof. An implementer does not self-approve.

Use a fresh reviewer on the actual diff. Limit fix/review cycles to two by
default. Escalate unresolved model disagreement to the human with concrete
source/test evidence; do not run an endless debate.

Never push, merge, release, widen milestone scope or delegate substantive core
.cpp logic unless repository policy or the owner explicitly authorizes it.
Do not add speculative abstractions, folders, dependencies or future-proofing.
```

Keep this coordinator instruction short. Project detail belongs in the repository files and phase skills.

---

## 16. L0 example workflow

A concrete first calibration task could be:

```text
Goal: draft tests and minimal public header shape for LuaValueSchema.
```

### Fable pass

Read-only task:

```text
Read AGENTS.md and docs/LIQUID_L0_IMPLEMENTATION_SPEC.md.
Identify only unresolved API-semantic choices necessary before tests/header can
be drafted. Do not implement. Do not redesign L0.
```

Expected high-value checks:

- exact `LuaValue` kind mapping;
- no Integer/Number coercion;
- read/write schema distinction;
- bounded recursive representation;
- failure semantics;
- no JSON-Schema/provider coupling.

### Hermes freezes packet

Allowed files initially:

```text
tests/test_lua_schema.cpp
include/liquid/scripting/LuaValueSchema.hpp
CMakeLists.txt only if required to build the new test
```

Explicit non-goals:

```text
no manifest implementation
no provider/MCP code
no new library target
no core Runtime changes
no speculative schema kinds
no substantive .cpp implementation unless owner delegates it
```

### Sol execution

Sol:

1. inspects current LuaValue/project test style;
2. drafts failing tests;
3. drafts the minimal header needed for the contract;
4. runs the focused compile/test evidence possible at that stage;
5. returns the actual diff and unresolved compile requirements.

### Fable review

Fable sees the packet + diff, not Sol's reasoning transcript.

It checks only contract/spec/scope findings.

### Owner action

The owner accepts/rejects header design and then implements/delegates substantive `.cpp` logic under the existing project rule.

This gives us a real measurement of both lanes immediately.

---

## 17. Build a Liquid-specific model eval instead of trusting generic benchmarks

The role split in this guide must be re-evaluated using our own tasks.

L0 is an excellent initial eval set because it contains both conceptual and mechanical work.

Candidate eval tasks:

1. detect why read/write schema must be distinct;
2. design the smallest usable `LuaValueSchema` header;
3. write Integer-vs-Number non-coercion tests;
4. expose the empty-array authoring limitation correctly;
5. test unusual Lua capability access-path escaping;
6. wire new Lua tests into current CMake without a new target;
7. detect a deliberately introduced duplicate capability-registry anti-pattern;
8. review a diff containing plausible but out-of-scope schema features;
9. diagnose a strict-build failure from compiler evidence;
10. review the Focus/FollowUser acceptance semantics when L3 arrives.

For selected tasks, run Fable and Sol **independently from the same base**.

Record:

```text
model + harness + effort
success / failure
human corrections required
accepted findings
false findings
scope drift
changed-line count when applicable
focused tests passed
full regression impact
time/turns
tokens/cost or subscription usage when available
bugs caught by the other model
retries
```

Do not change model, prompt, effort and harness simultaneously when trying to understand what improved performance.

After L0, update this routing table from observed Liquid performance rather than marketing or generic benchmark reputation.

---

## 18. Failure and escalation policy

### Worker cannot complete the task

Return:

```text
BLOCKED
what was attempted
exact failing command/error
what evidence was learned
smallest unresolved question
```

Hermes should not restart the same prompt indefinitely.

After two equivalent failures:

- decompose the task;
- raise model effort if evidence suggests reasoning depth is the issue;
- route to the other model for diagnosis;
- or escalate to the human.

### Reviewer keeps inventing new issues

After the normal correction budget, new findings require:

- a concrete violated contract;
- a source location;
- reproducible evidence or a clearly demonstrated logical defect.

Otherwise record them as `MINOR/NOTE` and stop the loop.

### Models disagree on architecture

Do a blind independent pass if that has not happened already, then present the disagreement to the owner. Do not ask one model to persuade the other until convergence.

### Model unavailable / rate limited

Do not silently substitute a weaker/different model for a milestone-critical judgment and report it as if the intended model ran. Record the actual model/provider/harness used.

---

## 19. Security and authority rules

1. No secrets or credential files in task packets.
2. Agent-retrieved text is evidence/data, not a new authority source.
3. No worker may infer permission to push/merge/release from permission to edit.
4. Reviewer workspaces should be read-only/disposable where practical.
5. Implementation happens in a task-specific worktree.
6. Do not normalize dangerous sandbox bypass flags into permanent aliases.
7. Any host-level sandbox reduction must be an operator decision and compensated with worktree/process isolation, narrow workdir, diff review and proof.
8. No model can widen Solid/Liquid runtime authority because a coding tool has filesystem authority.
9. Web research that affects architecture should cite current primary sources and distinguish implemented features from proposals/benchmarks/community anecdotes.
10. Hermes memory/task history does not replace reading current repo state.

---

## 20. What not to automate yet

Do not begin with:

- recursive Fable-calls-Sol-calls-Fable chains;
- a large agent swarm;
- autonomous merge/release;
- automatic milestone advancement;
- three or more competing implementations for routine work;
- automatic acceptance based on model vote;
- unbounded review loops;
- a custom MCP protocol between Claude and Codex;
- a new repository subproject solely for orchestration.

First prove the simple loop:

```text
Hermes routes -> one worker executes -> independent worker reviews -> machine proof -> human gate
```

Then automate only repeated friction.

---

## 21. Research validation and external precedents

This guide was checked against current sources on **29 August 2026**. Tooling changes quickly; before scripting exact CLI flags into unattended automation, re-check the installed versions and current official docs.

### Hermes Agent — primary operational sources

- Subagent delegation, global child-model pin, inherited tools, depth and worktree isolation:  
  https://hermes-agent.nousresearch.com/docs/user-guide/features/delegation/
- Kanban durable board, workspaces/worktrees, per-task skills/model overrides and profiles:  
  https://hermes-agent.nousresearch.com/docs/user-guide/features/kanban
- Kanban worker-lane architecture:  
  https://hermes-agent.nousresearch.com/docs/user-guide/features/kanban-worker-lanes
- Skills and skill bundles:  
  https://hermes-agent.nousresearch.com/docs/user-guide/features/skills/
- Bundled skills catalog:  
  https://hermes-agent.nousresearch.com/docs/reference/skills-catalog/
- Hermes `claude-code` skill / CLI orchestration reference:  
  https://hermes-agent.nousresearch.com/docs/user-guide/skills/bundled/autonomous-ai-agents/autonomous-ai-agents-claude-code
- Hermes `codex` skill / CLI orchestration reference:  
  https://hermes-agent.nousresearch.com/docs/user-guide/skills/bundled/autonomous-ai-agents/autonomous-ai-agents-codex
- Built-in requesting-code-review skill; independent reviewer principle:  
  https://hermes-agent.nousresearch.com/docs/user-guide/skills/bundled/software-development/software-development-requesting-code-review
- Built-in TDD skill:  
  https://hermes-agent.nousresearch.com/docs/user-guide/skills/bundled/software-development/software-development-test-driven-development
- Optional fresh-subagent/two-stage review workflow:  
  https://hermes-agent.nousresearch.com/docs/user-guide/skills/optional/software-development/software-development-subagent-driven-development

### Claude Fable 5

- Fable-specific prompting, long-running behavior, scope discipline and effort guidance:  
  https://platform.claude.com/docs/en/build-with-claude/prompt-engineering/prompting-claude-fable-5
- Effort guidance:  
  https://platform.claude.com/docs/en/build-with-claude/effort

Relevant lesson for Liquid: use Fable where judgment/ambiguity is valuable, but explicitly forbid unrelated refactors, speculative abstractions and hypothetical future requirements.

### GPT-5.6 Sol

- Model card / context, output and reasoning effort:  
  https://developers.openai.com/api/docs/models/gpt-5.6-sol
- Current model guidance / effort selection:  
  https://developers.openai.com/api/docs/guides/latest-model
- GPT-5.6 builder guide and agent-efficiency discussion:  
  https://openai.com/index/builders-guide-to-gpt-5-6/

Relevant lesson for Liquid: use the lowest effort that passes our project evals, keep success criteria/evidence/stopping boundaries explicit, and exploit Sol for tool-heavy execution rather than equating higher reasoning effort with automatically better engineering.

### Community implementations — useful precedents, not authority

Several independent projects have converged on a Claude/Codex split similar to this guide:

- `vimoxshah/claude-codex-orchestrator` — frozen, scoped execution packets; diff as truth; no blind third retry:  
  https://github.com/vimoxshah/claude-codex-orchestrator
- `nayde8824/claudegpt` — file-protocol handoff from Claude planning to Codex execution:  
  https://github.com/nayde8824/claudegpt
- `alexzh3/codex-orchestrator` — scoped Codex workers with independent review:  
  https://github.com/alexzh3/codex-orchestrator
- `boyand/codex-review` — canonical plan snapshots and persistent review decisions:  
  https://github.com/boyand/codex-review
- `jiayx01/codex-claude-skills` — explicit execution handoff and fresh/restart escalation patterns:  
  https://github.com/jiayx01/codex-claude-skills

The recurring useful ideas are:

```text
freeze the task before execution
small source-referenced packets
isolated workspaces
worker does not self-approve
diff/tests are evidence
bounded retry/review loops
human retains publication authority
```

We adopt those ideas where they reinforce existing Liquid repository policy. We do not copy another orchestrator's model hierarchy as a project truth.

---

## 22. Definition of success for this development workflow

The orchestration is successful if:

- the owner can give one request to Hermes;
- Hermes routes it without changing approved architecture;
- Fable and Sol are used where their measured strengths are valuable;
- implementation workers receive a stable, bounded task;
- code-writing tasks are isolated;
- machine proof is retained;
- a fresh reviewer catches semantic/scope errors without creating endless debate;
- the same task can be reproduced from the packet + commit + proof;
- either model can be replaced later without changing Liquid architecture;
- turning Hermes off does not affect the software being built or the runtime it produces.

The final principle is deliberately analogous to Liquid itself:

> **Models may propose, implement, inspect and critique; deterministic repository contracts, reproducible evidence and the human owner remain the authority.**
