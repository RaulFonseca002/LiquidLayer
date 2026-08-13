# Threading and Ownership Contract

`World`, `Runtime`, systems, adapters during synchronous dispatch, and writable event stores are confined to the creating thread. Public entry points reject use from another thread before mutation. Hosts provide any broader synchronization.

`FeedbackSender` is the deliberate cross-thread boundary. It is thread-safe, bounded, and accepts immutable `EffectReport` values from concurrent producers. Queue overflow is reported to the producer and never silently drops a report. Shutdown prevents new sends, wakes waiters, and permits deterministic draining or discard according to the selected close operation. Destroying `Runtime` invalidates its senders safely.

Runtime snapshots the queued reports at the beginning of a frame. Reports arriving after that snapshot wait for the next frame. Immediate feedback is limited to a report returned synchronously by the dispatch call; asynchronous reports always use the queue.
