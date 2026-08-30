---
title: Learn NGIN.Memory
description: Choose and compose NGIN.Base allocators, storage, ownership helpers, and allocator-aware containers safely.
---

# Learn NGIN.Memory

Memory in NGIN is built around a small, performance-first allocator concept and a set of composable utilities:

- Allocators are **header-only**, typically **value-stored handles**, and can be composed (tracking, thread-safety, fallback).
- Allocation is **explicit** (size + alignment) and **deterministic** when you choose deterministic allocators (e.g. arenas).
- Containers and smart pointers are **allocator-aware** and do not require the standard library allocator model.

## Start here

If this is your first time using NGIN.Memory, follow this path:

1. [Choose an allocator](./memory/choosing-allocator.md) from the lifetime and
   capacity you actually need.
2. [Use arenas and pools](./memory/arenas-pools.md) when reset or bounded
   capacity is part of the design.
3. [Choose owned or borrowed allocation](./memory/ownership.md) before storing
   an allocator in a container or smart pointer.
4. [Compose and diagnose allocators](./memory/composition-diagnostics.md) for
   fallback, tracking, debug, and thread-safe wrappers.
5. Use the [Memory API reference](../../reference/cpp/base/memory-containers.md) for
   exact declarations, failure, and ownership contracts.

The basic flow is:

```text
lifetime/capacity policy → allocator value or borrowed ref → allocation owner
           │                                              │
           └─ decides reset/free behavior                 └─ must not outlive policy
```

Allocation functions return `nullptr` for exhaustion or invalid requests.
Owning factories and containers may translate that into `std::bad_alloc`; read
the exact API contract rather than assuming one universal failure style.

The design goal is “real-time friendly” primitives:

- No hidden global heap abstractions (unless you explicitly use the system allocator).
- Avoid surprise allocations (SBO where appropriate, explicit `Reserve`/`Rehash`).
- Make routing decisions explicit (cookie-first, header fallback) rather than guessing with `Owns()`.

## Why This Allocator Model?

NGIN allocators are designed to be **small value types** that you can pass by value, store in containers, and compose at
compile time. This avoids:

- virtual dispatch and global state in hot paths,
- implicit synchronization,
- and “allocator object” APIs that accidentally become part of your ABI.

The result is a model that stays predictable under pressure: a call site always specifies **bytes + alignment**, and any
optional behavior (instrumentation, routing, reporting) is layered via traits or composition.

## Architecture

NGIN’s memory stack is split across two namespaces:

- `NGIN::Memory`: allocator concept/traits, concrete allocators, decorators, allocation helpers, smart pointers.
- `NGIN::Containers`: allocator-aware containers (`Vector`, `BasicString`, `FlatHashMap`, …).

The key idea is: **a minimal allocator concept for hot paths**, with optional capabilities surfaced via **traits**.

## Core Allocator Model

### `AllocatorConcept`

An allocator is any type that satisfies:

- `void* Allocate(std::size_t bytes, std::size_t alignment) noexcept`
- `void Deallocate(void* ptr, std::size_t bytes, std::size_t alignment) noexcept`

The size/alignment parameters to `Deallocate` may be ignored by implementations, but callers should pass the same values
they used when allocating to preserve correctness and enable instrumentation.

Allocation exhaustion and unrepresentable size/alignment requests return `nullptr`. Exception-based containers and
owning factories translate that result to `std::bad_alloc`.

### Allocator Handles and Lifetime

NGIN containers and smart pointers store allocators **by value**, but that value is typically an **allocator handle**.

Rule of thumb:

- Allocator handles do **not** own backing memory unless explicitly documented (e.g. `LinearAllocator` owns its slab).
- If you borrow an allocator, use `AllocatorRef` and ensure the underlying allocator **outlives all allocations** made
  through that reference (including those held by containers/smart pointers).

This is especially relevant for `AllocatorRef` and `PolyAllocatorRef`, which are both non-owning views.

### `AllocatorTraits`

Rather than requiring a “fat” allocator interface, extra capabilities are probed and accessed via
`NGIN::Memory::AllocatorTraits<A>`:

- `MaxSize()` / `Remaining()` (informational; “unknown” defaults are acceptable)
- `AllocateEx()` returning `MemoryBlock` (rich allocation result: pointer, size, alignment, cookie)
- tri-state ownership query:
  - `Ownership::Owns`, `Ownership::DoesNotOwn`, `Ownership::Unknown`
  - `AllocatorTraits<A>::OwnershipOf(alloc, ptr)`

Important rule:

- **Never** treat `Ownership::Unknown` as “owns” when making a routing decision.

### Container Propagation Traits

Allocator propagation is controlled by `NGIN::Memory::AllocatorPropagationTraits<A>` with std-like defaults:

- move-propagation is enabled by default
- copy-assignment does not silently switch allocator unless opted in

This is used by allocator-aware containers such as `Vector` and `BasicString`.

## Choosing an Allocator (Quick Guide)

Use this as a starting point:

| Need | Recommended |
|------|-------------|
| General-purpose heap allocations | `SystemAllocator` |
| Fast temporary allocations with bulk reset | `LinearAllocator` |
| Fixed-size allocations with bounded capacity | `FixedBlockAllocator` |
| Mixed small allocations with bounded capacity | `SegregatedPoolAllocator` |
| Canary, poisoning, and invalid-free diagnostics | `DebugAllocator<Inner>` |
| Fixed-capacity typed construction | `ObjectPool<T, Capacity>` |
| “Try A then B” without relying on `Owns()` | `TaggedFallbackAllocator` |
| “Try A then B” where both can reliably `Owns()` | `FallbackAllocator` |
| Instrumentation (bytes/counts/peaks) | `TrackingAllocator<Inner>` |
| Thread-safe wrapper around a stateful allocator | `ThreadSafeAllocator<Inner, Lockable>` |
| Rare dynamic dispatch over “some allocator” | `PolyAllocatorRef` |

## Concrete Allocators

### `SystemAllocator`

`NGIN::Memory::SystemAllocator` is a stateless allocator that forwards to platform aligned allocation primitives. Use it
as the default “heap” backend.

```cpp
NGIN::Memory::SystemAllocator heap;
void* p = heap.Allocate(256, 64);
heap.Deallocate(p, 256, 64);
```

### `LinearAllocator`

`NGIN::Memory::LinearAllocator<Upstream>` is an owning bump allocator:

- Allocates one slab from an upstream allocator at construction.
- `Allocate` is O(1) with `std::align`.
- `Deallocate` is a no-op (memory is reclaimed by `Reset()` / `Rollback(marker)` or destruction).

```cpp
#include <NGIN/Memory/LinearAllocator.hpp>

NGIN::Memory::LinearAllocator<> frameArena(1u << 20); // 1 MiB
void* a = frameArena.Allocate(1024, 16);
auto  m = frameArena.Mark();
void* b = frameArena.Allocate(2048, 32);
frameArena.Rollback(m);
```

Use cases:

- frame allocators
- scratch allocators
- arena-backed container builds (e.g. build a vector, then discard the arena)

### `FixedBlockAllocator<BlockSize, BlockCount, Alignment, Upstream>`

`FixedBlockAllocator` obtains one slab from its upstream allocator during construction and divides it into a LIFO
freelist. It accepts requests no larger than `BlockSize` and no more aligned than `Alignment`; exhaustion returns
`nullptr` and never falls through to an implicit heap. `AvailableBlocks`, `Remaining`, and `InvalidDeallocations`
provide deterministic diagnostics. The allocator validates slab ownership, block boundaries, and immediate double
frees. It is deliberately not thread-safe.

### `SegregatedPoolAllocator<BlocksPerClass, Upstream>`

`SegregatedPoolAllocator` owns one slab partitioned into 16, 32, 64, 128, 256, and 512-byte classes. A request uses
the smallest available fitting class and can spill into a larger class when its preferred freelist is exhausted.
Requests above 512 bytes or above `alignof(std::max_align_t)` return `nullptr`. There is no hidden system-allocation
fallback. Wrap the pool in `ThreadSafeAllocator` when multiple threads share it.

### `DebugAllocator<Inner>`

`DebugAllocator` is an opt-in decorator. It writes a header and trailing canary, fills new payloads with `0xCD`,
fills released payloads with `0xDD`, and tracks live returned pointers so an invalid free can be rejected without
dereferencing an arbitrary address. `GetStats` reports live, invalid, and corrupted allocation counts. Tracking uses
a system-backed vector and is not intended for allocation-free hot paths; wrap it explicitly for shared use.

### `ObjectPool<T, Capacity, Upstream>`

`ObjectPool` is the typed layer over `FixedBlockAllocator`. `Create` returns `nullptr` on capacity exhaustion and
returns the slot to the freelist if a constructor throws. `Destroy` runs the destructor and recycles the slot. The
pool must outlive all of its objects.

## Decorator Allocators

Decorator allocators wrap an “inner” allocator and add behavior without changing call sites.

### `TrackingAllocator<Inner>`

`NGIN::Memory::TrackingAllocator<Inner>` counts current/peak bytes and allocation counts.

```cpp
using Tracked = NGIN::Memory::TrackingAllocator<NGIN::Memory::SystemAllocator>;
Tracked tracked{NGIN::Memory::SystemAllocator{}};

void* p = tracked.Allocate(128, 16);
tracked.Deallocate(p, 128, 16);
auto stats = tracked.GetStats();
```

Tracking relies on callers passing consistent sizes to `Deallocate` (or using helpers that do).

### `ThreadSafeAllocator<Inner, Lockable>`

`NGIN::Memory::ThreadSafeAllocator` serializes access to a stateful allocator via a lock.

```cpp
using ThreadSafe = NGIN::Memory::ThreadSafeAllocator<NGIN::Memory::LinearAllocator<>>;
ThreadSafe alloc{NGIN::Memory::LinearAllocator<>(4096)};
```

Notes:

- The lock type is customizable (`Lockable`), so you can choose a spin lock for short critical sections.
- Query methods (`MaxSize/Remaining/OwnershipOf`) are locked as well to avoid data races.
- `WithInner(callback)` runs direct inner-allocator access while holding the wrapper lock; no unlocked inner reference
  escapes the wrapper.

## Composite Allocators (Fallback + Routing)

### `FallbackAllocator<Primary, Secondary>` (requires precise ownership)

`NGIN::Memory::FallbackAllocator` tries primary first, then secondary. It routes deallocation only after one allocator
returns `Ownership::Owns` from `OwnershipOf()`.

Both allocators must advertise `HasPreciseOwnership`. `Ownership::Unknown` never selects a deallocation target.

### `TaggedFallbackAllocator<Primary, Secondary>` (recommended)

`NGIN::Memory::TaggedFallbackAllocator` is the performance-first, correctness-first fallback:

- allocation writes a small header before the returned pointer
- deallocation reads the header and routes without ownership queries
- `AllocateEx` uses the `Cookie` field to expose which sub-allocator served the allocation

Use this when precise `OwnershipOf()` support is unavailable.

Every pointer passed to `Deallocate` must have originated from the same tagged fallback instance. Inspecting memory
before an arbitrary foreign pointer is not a safe ownership test, so this allocator deliberately exposes no such claim.

Cost note:

- `TaggedFallbackAllocator` adds a small constant overhead per allocation (a header plus any alignment padding) in
  exchange for deterministic routing.

```cpp
using Arena = NGIN::Memory::LinearAllocator<>;
NGIN::Memory::TaggedFallbackAllocator fb{Arena{1024}, NGIN::Memory::SystemAllocator{}};

void* p = fb.Allocate(64, 16);
fb.Deallocate(p, 64, 16);
```

## Type Erasure

### `PolyAllocatorRef` (non-owning)

`NGIN::Memory::PolyAllocatorRef` is a type-erased allocator **reference** intended for rare dynamic dispatch cases. It
does not allocate and does not own the underlying allocator.

```cpp
NGIN::Memory::SystemAllocator heap;
NGIN::Memory::PolyAllocatorRef poly(heap);

void* p = poly.Allocate(128, 16);
poly.Deallocate(p, 128, 16);
```

This is intentionally a “slow path” compared to templated allocators.

## Allocation Helpers

`NGIN::Memory::AllocationHelpers` provides safe object/array construction on top of `AllocatorConcept`.

`Reallocate` provides a byte-oriented strong guarantee: it allocates and copies first, releases the original only on
success, and leaves the original allocation untouched when the new request cannot be served. Passing a new size of
zero releases the original and returns `nullptr`.

### Objects

```cpp
#include <NGIN/Memory/AllocationHelpers.hpp>

NGIN::Memory::SystemAllocator alloc;

struct Foo { Foo(int) {} };
Foo* f = NGIN::Memory::AllocateObject<Foo>(alloc, 42);
NGIN::Memory::DeallocateObject(alloc, f);
```

### Arrays

Arrays store a small header so `DeallocateArray` can reclaim in O(1) without a user-provided count.

```cpp
NGIN::Memory::SystemAllocator alloc;
int* arr = NGIN::Memory::AllocateArray<int>(alloc, 128);
NGIN::Memory::DeallocateArray(alloc, arr);
```

## Smart Pointers (Allocator-Aware)

Smart pointers live in `NGIN::Memory::SmartPointers.hpp`.

### `Scoped<T, Alloc>`

`Scoped` is a move-only unique owner (similar to `std::unique_ptr`) that stores an allocator handle.

Use when:

- You want explicit ownership with minimal overhead.
- You want arena-friendly lifetimes (pair it with `AllocatorRef` to an arena).

```cpp
#include <NGIN/Memory/SmartPointers.hpp>

auto s = NGIN::Memory::MakeScoped<int>(123);
// uses SystemAllocator by default
```

With a custom allocator handle:

```cpp
#include <NGIN/Memory/AllocatorRef.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Memory/SmartPointers.hpp>

NGIN::Memory::LinearAllocator<> arena(4096);
NGIN::Memory::AllocatorRef arenaRef(arena); // non-owning handle
auto s = NGIN::Memory::MakeScoped<int>(arenaRef, 123);
```

### `Shared<T, Alloc>` and `Ticket<T, Alloc>`

`Shared` is a reference-counted shared owner, and `Ticket` is a weak handle (similar to `std::weak_ptr`).

Use when:

- Ownership must cross subsystem boundaries or outlive obvious scopes.
- You accept the overhead of atomics and a control block.

- Control block is allocator-backed.
- Reference counting uses atomics.

## Allocator-Aware Containers

Containers in `NGIN::Containers` take an allocator handle type as a template parameter (defaulting to
`NGIN::Memory::SystemAllocator`) and store it by value.

### `Vector<T, Alloc>`

`NGIN::Containers::Vector` is a contiguous dynamic array that uses `Alloc::Allocate/Deallocate` directly.

```cpp
NGIN::Containers::Vector<int> v; // uses SystemAllocator
v.Reserve(1024);
v.PushBack(1);
```

Allocator propagation follows `AllocatorPropagationTraits<Alloc>` (move-propagation enabled by default).

### `BasicString<CharT, SBOBytes, Alloc, Growth, Traits>`

`NGIN::Text::BasicString` is a small-string-optimized string:

- SBO sized in **bytes** (works for any `CharT`)
- allocator-backed heap growth beyond SBO
- traits-aware comparisons and views
- contiguous iterators over code units
- null-terminated storage for interop

Important semantics:

- `Size()` counts code units
- indexing, substring, and search APIs are code-unit based
- Unicode validation and encoding conversion live in `NGIN::Text::Unicode`, not in `BasicString`

```cpp
using String = NGIN::Text::String; // alias in String.hpp
String s("hello");
s += " world";
```

### `FlatHashMap<Key, Value, Hash, KeyEqual, Alloc>`

`NGIN::Containers::FlatHashMap` is a flat open-addressing hash map:

- linear probing
- backward-shift deletion (no tombstones)
- **explicit lifetime buckets** (no default-constructing N keys/values)
- allocator-backed bucket storage

Important constraint:

- `Key` and `Value` must be **nothrow move constructible** (used during backward-shift relocation).

## Patterns

### Frame Allocation Pattern (Arena + Containers)

Use a frame arena for transient work, and pass an `AllocatorRef` handle into containers:

```cpp
#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Memory/AllocatorRef.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>

NGIN::Memory::LinearAllocator<> frameArena(1u << 20); // 1 MiB
using FrameAlloc = NGIN::Memory::AllocatorRef<NGIN::Memory::LinearAllocator<>>;
FrameAlloc frame(frameArena);

NGIN::Containers::Vector<int, FrameAlloc> scratch(0, frame);
scratch.Reserve(1024);
// ... fill scratch ...

frameArena.Reset(); // reclaim everything from this frame
```

### Debug vs Release Composition

In debug builds you can wrap allocators for observability:

```cpp
using DebugAlloc = NGIN::Memory::ThreadSafeAllocator<
    NGIN::Memory::TrackingAllocator<NGIN::Memory::SystemAllocator>>;
```

In release builds, prefer the simplest allocator that meets your requirements (often `SystemAllocator` or an arena).

## Design Notes / Invariants

- Prefer `AllocatorTraits::AllocateEx` / cookies for routing and tooling; avoid guessing with `Owns()`.
- Treat allocator handles as cheap values; for borrowed allocators, use `AllocatorRef`.
- `LinearAllocator` is not thread-safe by design; wrap it if you need cross-thread access.
- Hash map erase and rehash can invalidate iterators/references; treat lookups as ephemeral unless you control mutation.

## Common Pitfalls

- Passing inconsistent sizes to `Deallocate` (breaks tracking and can break allocators that validate sizes).
- Using `FallbackAllocator` with allocators that can’t reliably answer `Owns()`. Prefer `TaggedFallbackAllocator`.
- Assuming erase in a flat hash map only affects the erased element (backward-shift deletion can relocate neighbors).
- Using a non-thread-safe allocator from multiple threads without a wrapper.

## ConcurrentHashMap reclamation

`ConcurrentHashMap` owns one reclamation domain per shard; it does not use a process-global epoch or hazard registry.
Reader registrations are scoped to the internal read guard used by `Contains`, `TryGet`, `GetOptional`, and
`ForEach`. Nested reads therefore create independent registrations, and normal guard destruction deregisters the
reader.

Choose the reclamation policy according to the synchronization contract:

- `ManualQuiesce` never reclaims retired chains or tables from a writer operation. The caller must invoke `Quiesce`
  only at an externally established point where no read can be active or start concurrently. Debug builds assert
  this precondition for readers already active in the map.
- `LocalEpoch` tags retired storage with a shard-local epoch. A reader pins its entry epoch, so one stalled reader
  delays every later retirement in that shard. `Poll` advances the epoch and reclaims records older than every active
  reader.
- `HazardPointers` publishes two hazards per read: the current table and the immutable bucket-chain head. A stalled
  reader delays only records matching those hazards; unrelated retired records remain reclaimable.

`Quiesce` on either automatic policy waits for current readers and drains retired storage. Map destruction does the
same, but object lifetime still requires callers to prevent new operations from starting once destruction begins.
An abandoned reader guard can therefore stall quiescence indefinitely; the library does not time out and reclaim
potentially observed storage.

`PendingRetired`, `ReclaimedRetired`, and `ActiveReaders` are deterministic diagnostic counters for tests and
operational inspection. Counts refer to reclamation records (a record may own an entire cloned chain or table), not
individual nodes. User nodes and tables use the map's allocator; small reclamation metadata records use the system
heap so their lifetime is independent of allocator state during deferred destruction.
