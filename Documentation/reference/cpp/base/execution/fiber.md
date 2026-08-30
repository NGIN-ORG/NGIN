---
title: Fiber
description: Code reference for optional stackful fibers, stack allocation, resume outcomes, and current-fiber helpers.
---

# `Fiber`

**Header:** `<NGIN/Execution/Fiber.hpp>`  
**Namespace:** `NGIN::Execution`  
**Target:** `NGIN::Base::Execution`  
**Defined:** [`Fiber.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Execution/Fiber.hpp#L148)

## Availability

The operational API exists when `NGIN_EXECUTION_HAS_STACKFUL_FIBERS != 0`.
When disabled, use triggers a compile-time requirement failure. Code supporting
both configurations should branch at build time.

## Supporting types

```cpp
struct FiberOptions {
    UIntSize stackSize;
    bool guardPages;
    UIntSize guardSize;
    FiberAllocatorRef allocator;
};

enum class FiberResumeResult : UInt8 {
    Yielded,
    Completed,
    Faulted,
};
```

`FiberAllocatorRef` is a non-owning type-erased stack allocator. `System()`
uses aligned global allocation. `From(allocator)` borrows a compatible
allocator, which must outlive the fiber and its stack.

## Fiber members

```cpp
Fiber();
explicit Fiber(UIntSize stackSize);
explicit Fiber(FiberOptions options);
Fiber(Job job, UIntSize stackSize = DEFAULT_STACK_SIZE);
Fiber(Job job, FiberOptions options);

void Assign(Job job);
bool TryAssign(Job job) noexcept;
FiberResumeResult Resume() noexcept;
std::exception_ptr TakeException() noexcept;
bool HasJob() const noexcept;
bool IsRunning() const noexcept;

static void EnsureMainFiber();
static bool IsMainFiberInitialized() noexcept;
static bool IsInFiber() noexcept;
static void YieldNow() noexcept;
```

Fibers are move-only, reusable stack owners. They are thread-affine. `Assign`
terminates for an empty job, invalid fiber, wrong owner thread, or running
fiber. `TryAssign` returns `false` only for an already running/occupied valid
fiber; it retains the other preconditions.

`Resume` runs until yield, completion, or captured exception. After `Faulted`,
`TakeException` transfers the stored exception.

## `ThisFiber`

`ThisFiber::IsInFiber`, `IsInitialized`, and `YieldNow` forward to the static
fiber operations for the calling thread.

