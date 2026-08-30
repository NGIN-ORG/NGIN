---
title: Allocator composition
description: Code reference for fallback, tagged fallback, tracking, debug, and thread-safe allocator wrappers.
---

# Allocator composition

## `FallbackAllocator<Primary, Secondary>`

Tries primary, then secondary. It requires precise ownership so deallocation
can query and route to the correct allocator. `MaxSize`/`Remaining` combine
capabilities subject to overflow-safe implementation rules.

## `TaggedFallbackAllocator<Primary, Secondary>`

Allocates extra header/alignment space and records a route tag before the user
pointer. `AllocateEx` exposes the route in its cookie. Use this when ownership
queries are not definitive. The header must not be modified and the exact
returned pointer must be deallocated.

## `TrackingAllocator<Inner>`

Stores an inner allocator and `AllocationStats`. `GetStats()` returns counters
for observed allocations, deallocations, live/peak bytes, and related metrics.
Its precision is limited to operations routed back through the same wrapper.

## `DebugAllocator<Inner>`

Adds allocation headers, canaries, poisoning, live records, and
`DebugAllocatorStats`. It reports precise ownership for decorated blocks and
counts invalid/corrupt deallocations. It is diagnostic infrastructure with
intentional size/time overhead.

## `ThreadSafeAllocator<Inner, Lockable>`

Locks around allocator operations and optional queries. `WithInner(callback)`
runs direct inner access while holding the wrapper lock and avoids exposing an
unprotected inner reference. It does not synchronize objects stored in the
allocated memory.

## Lifetime

Wrappers store their inner allocators by value. Whether that value owns or
borrows a resource depends on the inner type. Destroy every allocation user
before destroying the outermost composition.

