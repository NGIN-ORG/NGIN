---
title: NetworkDriver API
description: API reference for the explicit readiness runtime behind coroutine socket operations.
---

# `NetworkDriver` API

**Header:** `<NGIN/Net/Runtime/NetworkDriver.hpp>`

`NetworkDriver::Create(NetworkDriverOptions)` returns a unique runtime instance.
The driver is non-copyable and owns platform readiness/completion state.

The public loop controls are `Run()`, `PollOnce()`, and `Stop()`. `Run` owns a
blocking driver loop, `PollOnce` performs one integration cycle, and `Stop`
requests the running loop to finish.

`WaitUntilReadable` and `WaitUntilWritable` return cancellation-aware tasks.
Socket wrappers dispatch through `SubmitSend`, `SubmitReceive`, `SubmitSendTo`,
`SubmitReceiveFrom`, `SubmitConnect`, and `SubmitAccept`.

Every async operation receives:

- an `Async::TaskContext&` controlling continuation execution;
- a socket/handle owned elsewhere;
- borrowed buffers where applicable;
- an optional `CancellationToken`.

The driver, socket, task context/executor, cancellation state, and borrowed
buffer must survive until completion. Cancellation is cooperative and must be
driven to a terminal task state before dependent storage is destroyed.

`NetworkDriverOptions` controls backend/runtime resource policy. Platform
implementations may use IOCP, epoll, kqueue, or a fallback while preserving the
same public contract.

**Defined:** [`NetworkDriver.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Net/Runtime/NetworkDriver.hpp)
