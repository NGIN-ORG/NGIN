---
title: Asynchronous file I/O
description: Own FileSystemDriver, TaskContext, async handles, buffers, cancellation, and completion until terminal state.
---

# Asynchronous file I/O

Async file operations require an explicitly owned `FileSystemDriver` and
`TaskContext`:

```cpp
NGIN::IO::FileSystemDriver driver;
auto context = driver.MakeTaskContext();
NGIN::IO::LocalFileSystem fs;

auto task = NGIN::IO::ReadAllBytesAsync(fs, context, path);
auto operation = NGIN::Async::Spawn(context, std::move(task));
```

The driver selects a platform backend/fallback according to its options and
capabilities. Completion resumes through the context executor. Both driver and
context must outlive the operation.

`AsyncFileHandle` is move-only type-erased state with `ReadAsync`, `WriteAsync`,
offset variants, `FlushAsync`, `CloseAsync`, and `IsOpen`. The static operations
table used to construct a custom handle must also outlive every handle/move.

Caller spans passed to read/write must stay valid and unmoved until the task
terminates. Cancellation is cooperative: cancel, then keep driver/executor and
buffers alive until completion reports success/domain error/canceled/fault.

Native support varies. Path lookup and directory operations may use fallback
work even when file transfer uses io_uring/IOCP. For startup/tools, synchronous
I/O is often simpler and more predictable.

