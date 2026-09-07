---
title: Coroutine sockets and the I/O runtime
description: Bind sockets to a shared IO::Runtime and resume tasks through their own executor.
---

# Coroutine sockets and the I/O runtime

```cpp
NGIN::Execution::ThreadPoolScheduler scheduler(1);
NGIN::IO::Runtime io;
NGIN::Async::TaskContext ctx(scheduler);
NGIN::Net::TcpSocket socket(io);
if (!socket.Open()) return;

// At the application boundary; do not block a task worker with SyncWait.
auto result = NGIN::Async::SyncWait(ctx, socket.ConnectAsync(
        ctx, {NGIN::Net::IpAddress::LoopbackV4(), 9000}));
if (!result.Succeeded()) return;
```

The runtime initializes networking on the first async operation and owns a
background polling thread by default. It can also serve `LocalFileSystem(io)`
without requiring file workers in a networking-only application. Sockets bind
once; accepted sockets inherit the listener binding, and transport adapters
preserve it when taking ownership of a socket.

`TaskContext` selects the continuation executor and cancellation. A socket
method's optional explicit cancellation token is linked with the context token.
The runtime and all borrowed buffers must remain alive until completion.

For application-managed polling, construct the runtime with
`.network = {.mode = NGIN::IO::Runtime::NetworkMode::Manual}` and call `Run()`
or `PollOnce()` on one polling thread. Background mode rejects those calls.
Cancel and await pending tasks before calling `Stop()` or destroying the runtime.
The task executor must accept completion submissions from the I/O threads.

See [Hello.IO](https://github.com/NGIN-ORG/NGIN/tree/main/Examples/Hello.IO)
for a complete loopback application, file operations, and cancellation.
