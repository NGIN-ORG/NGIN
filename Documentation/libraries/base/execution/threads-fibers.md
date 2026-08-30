---
title: Threads and fibers
description: Own native threads and optional stackful fibers with explicit destruction, stack, exception, and thread-affinity rules.
---

# Threads and fibers

## Native thread ownership

`Thread` owns a platform thread handle and is move-only:

```cpp
NGIN::Execution::Thread::Options options;
options.name = NGIN::Execution::ThreadName {"Indexer"};
options.onDestruct = NGIN::Execution::Thread::OnDestruct::Join;

NGIN::Execution::Thread thread([] {
    RunIndexer();
}, options);

thread.Join();
```

The destruction choices are deliberately explicit:

| Policy | Destructor behavior | Risk |
| --- | --- | --- |
| `Join` | Blocks until exit | Can block indefinitely if no stop protocol exists |
| `Detach` | Releases the handle without waiting | Captured state must independently outlive the thread |
| `Terminate` | Terminates if still joinable | Makes forgotten ownership immediately visible |

`Terminate` is the default. `WorkerThread` always changes destruction to
`Join`, which is usually suitable for an owned background worker after a stop
flag/wakeup protocol is installed.

Thread name, affinity, and priority are best-effort platform operations. Check
their Boolean result. `ThisThread` provides current ID, hardware concurrency,
yield, sleep, and current-thread name/affinity/priority operations.

## Stackful fibers

```cpp
#if NGIN_EXECUTION_HAS_STACKFUL_FIBERS
NGIN::Execution::Fiber fiber([] {
    StepOne();
    NGIN::Execution::Fiber::YieldNow();
    StepTwo();
});

auto first = fiber.Resume();
auto second = fiber.Resume();
#endif
```

A fiber owns a stack and remains bound to its owner thread. `Resume()` returns
`Yielded`, `Completed`, or `Faulted`; `TakeException()` transfers a captured
exception from a faulted job. Do not destroy or move a running fiber, resume it
concurrently, or call `YieldNow` outside a fiber.

`FiberOptions` selects stack size, best-effort guard pages, and a borrowed
`FiberAllocatorRef`. A custom allocator must outlive the fiber and every stack
allocated through it.

## Choose the abstraction

- Use a scheduler when you need queued units of work.
- Use `Thread` when you own a dedicated OS execution resource.
- Use C++ coroutines/NGIN.Async for suspendable typed operations.
- Use `Fiber` for stackful cooperative suspension of existing synchronous call
  chains when the platform build supports it.

Each adds a distinct ownership and shutdown boundary.

