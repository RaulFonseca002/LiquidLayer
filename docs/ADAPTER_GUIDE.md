# Adapter Integration Guide

The normative rules are in [ADAPTER_CONTRACT.md](ADAPTER_CONTRACT.md). This guide describes the host responsibilities around that contract.

## Register a route

Give every route a stable identity and declare whether the destination provides native idempotency and read-after-write reconciliation. Keep transport credentials and protocol clients inside the adapter; never pass `World`, registries, component slots, or component pointers across this boundary.

## Dispatch safely

1. Accept the immutable `EffectCommand` selected by Runtime.
2. Use its stable `SessionId` and `CommandId` as the idempotency identity.
3. Dispatch only after Runtime has durably recorded command issuance and the current attempt.
4. Return a synchronous report only when the adapter actually knows the result during dispatch.
5. Send later results through the bounded, thread-safe `FeedbackSender`.

Queue rejection means feedback was not accepted; the adapter must apply its documented backpressure/reconciliation policy rather than silently dropping authoritative reports. Adapter exceptions must be converted to bounded dispatch evidence.

## Interpret outcomes

Only a validated applied report can establish observed state. Rejected, failed, timed-out, missing, superseded, and indeterminate results do not imply application. Duplicate identical reports are safe; conflicting duplicates are invalid evidence.

The default retry schedule reuses the command ID at 250 ms, 500 ms, 1 s, 2 s, and 4 s, without jitter, until the 30-second overall timeout. The host's explicit monotonic frame time advances this schedule. An adapter must not create an independent retry loop that defeats Runtime ordering.

After crash uncertainty, a route without native idempotency or reconciliation becomes indeterminate. The target stays blocked until the host supplies explicit reconciliation; guessing from transport success is forbidden.

For deterministic development, use `Liquid::Simulation` to model latency, normalization, rejection, silence, duplicates, stale or out-of-order reports, and crash/retry recovery before connecting an external transport.
