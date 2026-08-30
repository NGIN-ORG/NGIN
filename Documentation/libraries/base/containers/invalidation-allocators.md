---
title: Container invalidation and allocators
description: Reason about references, iterator invalidation, allocator propagation, movement, arena reset, and thread safety.
---

# Container invalidation and allocators

## Address lifetime

Container ownership does not guarantee stable element addresses. Before
retaining a pointer/reference/iterator, list every operation that may grow,
rehash, shift, erase, move, or destroy its source.

Prefer stable logical identity (key/index plus version) and reacquire after
mutation. Reserve can prevent capacity growth up to the reserved bound but does
not prevent invalidation from erase/insert shifting.

## Allocator movement

Allocator propagation traits decide whether copy/move assignment changes the
destination allocator. If storage cannot be stolen, a move may allocate and
relocate elements. This affects complexity, exception behavior, and address
stability.

Never compare only allocator types to decide compatibility. Stateful handles of
the same C++ type can refer to different arenas/resources.

## Arena-backed containers

Correct order:

```text
construct arena → construct containers → use → destroy containers → reset/destroy arena
```

`Clear()` destroys elements but often keeps allocated capacity. Resetting the
arena after `Clear` while the container remains usable leaves its capacity
pointer dangling. Scope/destroy the container first or reconstruct it before
reuse.

## Thread safety

Ordinary Vector/String/FlatHash containers require external synchronization
for shared mutation and for reads concurrent with mutation. Making the
allocator thread-safe protects allocator state only. Use confinement, locks,
immutable snapshots, or a purpose-built concurrent container.

## Exceptions and partial change

Growth and element construction can fail. Each operation documents whether it
preserves the original state or provides a completed prefix/basic guarantee.
Types with nothrow move operations enable stronger relocation behavior. Do not
build a recovery policy from assumptions borrowed from a different container.

