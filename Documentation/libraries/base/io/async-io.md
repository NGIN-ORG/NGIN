---
title: Asynchronous file I/O
description: Own IO::Runtime, TaskContext, async handles, buffers, cancellation, and completion until terminal state.
---

# Asynchronous file I/O

Async file operations require an explicitly owned `IO::Runtime` and
`TaskContext`:

```cpp
NGIN::Execution::ThreadPoolScheduler scheduler(1);
NGIN::IO::Runtime io;
NGIN::Async::TaskContext context(scheduler);
NGIN::IO::LocalFileSystem fs(io);

auto task = NGIN::IO::ReadAllBytesAsync(fs, context, path);
auto completion = NGIN::Async::SyncWait(context, std::move(task));
if (!completion.Succeeded()) { /* handle domain error, cancellation, or fault */ }
```

The runtime lazily selects a platform backend/fallback according to its options and
capabilities. Completion resumes through the context executor. Both runtime and
context must outlive the operation.

`AsyncFileHandle` is move-only type-erased state with `ReadAsync`, `WriteAsync`,
offset variants, `FlushAsync`, `CloseAsync`, and `IsOpen`. The static operations
table used to construct a custom handle must also outlive every handle/move.

Caller spans passed to read/write must stay valid and unmoved until the task
terminates. Cancellation is cooperative: cancel, then keep runtime/executor and
buffers alive until completion reports success/domain error/canceled/fault.

Native support varies. Path lookup and directory operations may use fallback
work even when file transfer uses io_uring/IOCP. For startup/tools, synchronous
I/O is often simpler and more predictable.

