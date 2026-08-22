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
