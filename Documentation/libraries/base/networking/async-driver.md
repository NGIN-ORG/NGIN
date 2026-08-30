---
title: Coroutine sockets and NetworkDriver
description: Drive cancellation-aware socket tasks through an explicit NetworkDriver and caller-owned executor.
---

# Coroutine sockets and `NetworkDriver`

Create one explicit readiness runtime and ensure it makes progress:

```cpp
auto driver = NGIN::Net::NetworkDriver::Create({});
NGIN::Execution::CooperativeScheduler scheduler;
NGIN::Async::TaskContext context {scheduler};

auto task = socket.ConnectAsync(
    context, *driver, endpoint, context.GetCancellationToken());
auto operation = NGIN::Async::Spawn(context, std::move(task));

while (!operation.IsCompleted()) {
    driver->PollOnce();
    scheduler.RunUntilIdle();
}
```

`Run()` lets the driver own a blocking readiness loop; `PollOnce()` integrates
one cycle into another loop; `Stop()` ends `Run`. No global driver exists.

Async socket methods return `Task<T, NetError>`. Networking failures are domain
errors, cancellation is the separate task cancellation state, and executor/
runtime failures are async faults.

Only retain caller buffers until send/receive task completion. Keep driver,
socket, context/executor, cancellation state, and buffers alive. Stop/cancel
new operations, drive them terminal, then destroy sockets and driver.

Platform backends differ (IOCP, epoll, kqueue, select fallback) but expose the
same public readiness contract. `ResourceExhausted` means required operation/
cancellation readiness state could not be allocated and registration failed.

