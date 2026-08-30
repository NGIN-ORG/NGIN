---
title: Allocator composition and diagnostics
description: Compose fallback, tracking, debug, and thread-safe allocators without losing routing or lifetime correctness.
---

# Allocator composition and diagnostics

## Tracking

```cpp
using Tracked = NGIN::Memory::TrackingAllocator<
    NGIN::Memory::SystemAllocator>;

Tracked allocator;
void* p = allocator.Allocate(128, 16);
auto stats = allocator.GetStats();
allocator.Deallocate(p, 128, 16);
```

Tracking records requested allocations/deallocations and peaks. Results are
meaningful only if the same wrapper observes both sides and callers supply
matching metadata.

## Debug allocator

`DebugAllocator<Inner>` adds headers, canaries, poisoning, live records, and
invalid-deallocation diagnostics. It has deliberate overhead and precise
ownership for its decorated allocations. Use it in diagnostic configurations,
then reproduce issues before assuming release behavior is identical.

## Thread-safe wrapper

`ThreadSafeAllocator<Inner, Lockable>` locks around allocation, deallocation,
queries, and `WithInner` callbacks. Do not retain an unlocked reference to the
inner allocator. The wrapper synchronizes allocator metadata—not constructed
objects or memory contents.

## Fallback routing

`FallbackAllocator<Primary, Secondary>` tries primary then secondary and uses
precise ownership to route deallocation. Both allocator types must support the
required precise ownership capability.

`TaggedFallbackAllocator` stores routing metadata before the returned aligned
address. It is the safer general choice when an allocator cannot definitively
classify every pointer. The metadata adds a small allocation overhead and must
remain intact.

## Composition order

Wrapper order changes what is observed and synchronized:

```cpp
using DebugThenTrack = TrackingAllocator<DebugAllocator<SystemAllocator>>;
using TrackThenDebug = DebugAllocator<TrackingAllocator<SystemAllocator>>;
```

The first tracks debug allocator requests; the second debug-wraps requests
whose upstream is tracked. Decide whether metrics should include wrapper
headers/guards and whether one outer lock should cover every inner operation.

## Failure and shutdown

Allocation APIs return `nullptr` on resource failure. Diagnostics must not
recursively depend on the same exhausted allocator. Before destroying a
stateful allocator, destroy all objects/containers/control blocks and stop all
threads that can access it.

