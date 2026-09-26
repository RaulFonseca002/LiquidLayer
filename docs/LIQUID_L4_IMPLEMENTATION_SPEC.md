# L4 — Bounded authoring session journal

**Status:** specified; inactive. **Dependency:** L3 accepted and landed.
Apply [the common contract](LIQUID_IMPLEMENTATION_CONTRACT.md).

## Outcome and scope

Record which exact artifacts were proposed/evaluated and, in L5, approved and
operated. Query these facts without contaminating Solid Event Format v1.
Introduce the journal before operations so approval evidence is not an afterthought.

This milestone is a finite in-memory session journal, not a durable audit log.
It does not survive process loss, reconstruct a live authoring session, or
provide tamper evidence. Solid replay continues to answer deterministic runtime
questions independently. Do not copy Solid checkpoint retention as an unbounded
history solution: v1 checkpoints retain historical arrays and eventually hit
Value limits.

## Data and API

Add `AuthoringJournal`, `JournalRecord`, `JournalPage`, `JournalCursor` and
`AuthoringSession::history(CallerContext, ScopeId, optional<JournalCursor>, limit)`.
Cursor contains the authoring session ID, scope ID and last delivered global
sequence. Requests default to 64 records; maximum is 128 and 1 MiB per page.
Reject wrong-session/scope/future cursors. A valid cursor at the end returns an
empty page with `hasMore=false`; polling does not create records.

Records contain schemaVersion=1, monotonically increasing JournalSequence,
monotonic host time, scope/revision, kind, relevant artifact IDs and a bounded
outcome. Kinds are `ScopeCreated`, `ScopeChanged`, `ScopeRevoked`,
`ProposalSubmitted`, `EvaluationCompleted`, `ApprovalGranted`,
`OperationStarted`, `OperationCompleted`, `InterventionRequired`.
L4 reserves the L5 kinds; only L5 emits them.

Store references to immutable proposal/evaluation records, not duplicate prompts
or raw traces. Each record is at most 16 KiB logical payload; diagnostics follow
the common 4 KiB limit. Optional host-supplied provenance is limited to model
label/provider label, 128 bytes each; absence is valid. It is descriptive,
never an authority or reproducibility claim. Do not record API keys,
conversation text, sensor dumps or arbitrary dynamic context by default.

Solid correlations use its actual SessionId and optional frame/RecordId ranges
when known. Do not invent a RecordId for an in-memory action that has not yet
been journaled by Runtime. Live activation occurs between frames; L5 records
its authoring outcome immediately and the first subsequent frame can later be
correlated by inspection. Historical references never masquerade as live handles.

## Admission and publication

The journal is append-only for the session. Sequence numbers never recycle.
No automatic eviction, compaction, disk persistence or background queue.
The common record/byte limits and a 64 MiB aggregate session payload ceiling
cover all retained scope/proposal/evaluation/approval/operation data as well.
Admit and reserve complete output capacity before accepting new work.

Introduce a private reservation object that allocates record storage and its
maximum payload before publication. Scope/proposal mutations commit their
record and repository update together or neither. Evaluation reserves its
terminal record before executing cases; case failure still has a recorded
EvaluationCompleted result with explicit failed status.

For L5 operations reserve two records (start and terminal), each 16 KiB,
before any live mutation. The start record is committed immediately before
the first topology action. Terminal storage is already allocated; completion
must not depend on a second fallible external journal callback. There is no
claim of atomicity with a process crash: losing this in-memory journal loses
both records. Runtime durable effects still follow Solid's own outbox rules.

Also support a host-private reserved pair per active managed behavior for a
stop attempt when ordinary journal capacity is exhausted. Activation requires
this reservation; it cannot consume the last capacity needed to attempt stop.
An unsuccessful stop consumes its evidence slots and enters NeedsIntervention;
further repair requires host action and an explicitly admitted new operation.
Native emergency cleanup remains possible through existing host authority,
but is outside automatic authoring success and must not be represented as
a recorded operation if it bypassed the journal.

## Queries and privacy

Filter records by authorized scope before pagination. Sequence gaps from other
scopes do not reveal record content; `hasMore` refers only to permitted records.
Return no global total. Known foreign record IDs do not bypass ownership.
The page cursor advances to the last returned permitted record; on an empty
page retain the supplied cursor. Ordering is by journal sequence, not wall time.

Artifact lookup applies the same checks as L1/L2. Public history provides facts
and bounded summaries; detailed private evaluation traces require trusted-host
access. A caller cannot retrieve another proposal's exact source by guessing
an evaluation ID. Revoked scope access fails; native owner access remains.

Session destruction does not mutate the live World. Approved behaviors may keep
running, but session references/history cannot be recovered automatically.
The host must retain the session while it needs Liquid control or explicitly
stop/manage those behaviors itself. No implicit session rotation hides this limit.

## Implementation steps and tests

| Step | Implement after its failing test | Required oracle |
| --- | --- | --- |
| L4.1 | Bounded typed journal and cursor | Ordered immutable records, exact bounds, counter exhaustion, empty page and end cursor, no silent eviction |
| L4.2 | Scope/proposal/evaluation event wiring | Failed admission leaves no partial record/state; failed evaluation is recorded; source identity matches immutable repository |
| L4.3 | Two-record and emergency-stop reservations | Allocation/capacity failure before mutation; reserved terminal publication works under ordinary exhaustion; no false crash durability |
| L4.4 | Scoped history and correlations | Foreign scope/session denied, sparse pagination correct, no raw private context, actual versus unavailable Solid correlation distinguished |

Register `authoring_journal`. Run
`ctest --test-dir build/strict -R '^authoring_(journal|scope|proposal|evaluation)$' --output-on-failure`
and common gates. No file I/O or remote service is necessary for these tests.

## Allowed files and exit

New: `include/liquid/authoring/AuthoringJournal.hpp`,
`src/authoring/AuthoringJournal.cpp`, `tests/test_authoring_journal.cpp`.
Existing: authoring session/types/evaluation, focused tests, CMake/consumer/docs.
Do not change EventTypes, replay, checkpoints or FileEventStore.

Exit: every admitted authoring artifact has correlated, queryable, bounded
session evidence, and L5 can reserve operation outcomes before live changes.
