# Adapter, Feedback, and Retry Contract

Adapters receive immutable `EffectCommand` values and a bounded `FeedbackSender`. They never receive `World`, registries, component slots, or component pointers. A route has stable identity and declares `AdapterCapabilities`, including native idempotency and read-after-write reconciliation.

`FeedbackTiming` is fixed for a session. Deferred mode applies all reports in the next frame feedback phase. Immediate mode may additionally apply only a report synchronously returned from dispatch, after systems and resolution. Asynchronous reports always wait for the next frame.

A command is issued only when selected encoded desire differs from both observed state and the current outstanding desired value. A new desire supersedes the older target command and cancels its retries. Reports are matched by session, command, route, target, and attempt contract. Duplicate identical reports are recorded without mutation; conflicting duplicates are rejected. Unknown, malformed, mismatched, stale, or cross-session reports never mutate state. Rejected, failed, timed-out, missing, and indeterminate outcomes never imply application.

The default retry schedule reuses the same `CommandId` at 250 ms, 500 ms, 1 s, 2 s, and 4 s, with no jitter and a 30-second overall timeout. Explicit monotonic frame time drives the schedule. After crash uncertainty, adapters lacking native idempotency or reconciliation make the command indeterminate and block the target until explicit host reconciliation.

The reusable idempotent dispatcher caches bounded outcomes in memory and, when configured, durably. Dispatch never occurs until command issuance and the current attempt are durably recorded.
