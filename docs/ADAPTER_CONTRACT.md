# Adapter, Feedback, and Retry Contract

Adapters receive immutable `EffectCommand` values and a bounded `FeedbackSender`. They never receive `World`, registries, component slots, or component pointers. A route has stable identity and declares `AdapterCapabilities`, including native idempotency and read-after-write reconciliation.

Route registration is atomic with its durable evidence. Runtime queries the
capabilities and prepares the complete registration/status batch before
publishing the route. If the batch append fails, neither the adapter nor its
derived reconciliation state becomes visible, and the same registration may
be retried without disturbing other routes.

`FeedbackTiming` is fixed for a session. Deferred mode applies all reports in the next frame feedback phase. Immediate mode may additionally apply only a report synchronously returned from dispatch, after systems and resolution. Asynchronous reports always wait for the next frame. Adapters may also publish `ExternalObservation` values for unsolicited device changes; observations carry a monotonically increasing target-local `StateRevision` and enter through the same bounded feedback channel.

A command is issued only when selected encoded desire differs from both observed state and the current outstanding desired value. A new desire—or disappearance of the selected desire—supersedes the older target command and cancels its retries. Reports are matched by session, command, route, target, and attempt contract. Applied reports and external observations are ordered by state revision. Duplicate identical revisions are recorded without mutation; conflicting or stale revisions are rejected. Unknown, malformed, mismatched, stale, or cross-session feedback never mutates state. Rejected, failed, timed-out, missing, and indeterminate outcomes never imply application.

The default retry schedule reuses the same `CommandId` at 250 ms, 500 ms, 1 s, 2 s, and 4 s, with no jitter and a 30-second overall timeout. Explicit monotonic frame time drives the schedule. After crash uncertainty, adapters lacking native idempotency or reconciliation make the command indeterminate and block the target until explicit host reconciliation.

The reusable idempotent dispatcher caches bounded outcomes in memory and, when configured, durably. Dispatch never occurs until command issuance and the current attempt are durably recorded.

## Implementing an adapter

*(formerly `docs/ADAPTER_GUIDE.md`, "Adapter Integration Guide"; text verbatim, headings demoted one level. Where it restates registration atomicity, the retry schedule or the indeterminate outcome, the contract text above is normative.)*

The normative rules are in [ADAPTER_CONTRACT.md](#adapter-feedback-and-retry-contract). This guide describes the host responsibilities around that contract.

### Register a route

Give every route a stable identity and declare whether the destination provides native idempotency and read-after-write reconciliation. Keep transport credentials and protocol clients inside the adapter; never pass `World`, registries, component slots, or component pointers across this boundary.

Registration becomes visible only after Runtime durably appends the route's
complete status batch. Capability discovery or durable append failure leaves
the route absent and retryable; it must not partially alter reconciliation
state or disturb an already registered route.

### Dispatch safely

1. Accept the immutable `EffectCommand` selected by Runtime.
2. Use its stable `SessionId` and `CommandId` as the idempotency identity.
3. Dispatch only after Runtime has durably recorded command issuance and the current attempt.
4. Return a synchronous report only when the adapter actually knows the result during dispatch.
5. Send later results through the bounded, thread-safe `FeedbackSender`.

Queue rejection means feedback was not accepted; the adapter must apply its documented backpressure/reconciliation policy rather than silently dropping authoritative reports. Adapter exceptions must be converted to bounded dispatch evidence.

### Interpret outcomes

Only a validated applied report can establish observed state. Rejected, failed, timed-out, missing, superseded, and indeterminate results do not imply application. Duplicate identical reports are safe; conflicting duplicates are invalid evidence.

The default retry schedule reuses the command ID at 250 ms, 500 ms, 1 s, 2 s, and 4 s, without jitter, until the 30-second overall timeout. The host's explicit monotonic frame time advances this schedule. An adapter must not create an independent retry loop that defeats Runtime ordering.

After crash uncertainty, a route without native idempotency or reconciliation becomes indeterminate. The target stays blocked until the host supplies explicit reconciliation; guessing from transport success is forbidden.

For deterministic development, use `Liquid::Simulation` to model latency, normalization, rejection, silence, duplicates, stale or out-of-order reports, and crash/retry recovery before connecting an external transport.
