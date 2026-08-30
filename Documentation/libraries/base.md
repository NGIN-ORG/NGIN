---
title: NGIN.Base
description: Foundational C++23 facilities with explicit runtimes, typed results, deterministic ownership, and allocator control.
---

# NGIN.Base

`NGIN.Base` is the foundational C++23 library used across NGIN. It can also be
adopted independently. Its design favors explicit runtimes, typed results,
deterministic ownership, and allocator control.

## Start here

1. Complete the [quick start](./base/quick-start.md).
2. Choose the subsystem closest to your problem.
3. Prefer the narrowest CMake component target that owns the API you use.
4. Use the [NGIN.Base C++ reference](../reference/cpp/base/index.md) for exact headers,
   types, ownership, errors, and failure modes.

## Subsystems

| Subsystem | What it solves | Learn | C++ API |
| --- | --- | --- | --- |
| Async | Coroutine tasks, results, cancellation, composition, and async streams | [Learn Async](./base/async.md) | [Symbols](../reference/cpp/base/async/index.md) |
| Execution | Executors, schedulers, native threads, and fibers | [Learn Execution](./base/execution.md) | [Symbols](../reference/cpp/base/execution.md) |
| Synchronization | Locks, semaphores, guards, and atomic conditions | [Learn Sync](./base/synchronization.md) | [Symbols](../reference/cpp/base/sync/index.md) |
| Memory | Allocators, arenas, pools, storage, and ownership helpers | [Learn Memory](./base/memory.md) | [Symbols](../reference/cpp/base/memory-containers.md) |
| Containers | Allocator-aware vectors, strings, and hash maps | [Learn Containers](./base/containers.md) | [Symbols](../reference/cpp/base/memory-containers.md#containers) |
| I/O | Paths, filesystems, file handles, processes, and libraries | [Learn I/O](./base/io.md) | [Symbols](../reference/cpp/base/io.md) |
| Networking | Addresses, sockets, coroutine I/O, transports, framing, and TLS | [Learn Networking](./base/networking.md) | [Symbols](../reference/cpp/base/networking.md) |
| Serialization | JSON and XML documents, events, builders, and writers | [Learn Serialization](./base/serialization.md) | [Symbols](../reference/cpp/base/serialization.md) |
| Cryptography | Providers, secure random, keys, encryption, certificates, and tokens | [Learn Crypto](./base/cryptography.md) | [Symbols](../reference/cpp/base/crypto.md) |
| Foundation map | Provider-free shared vocabulary and navigation | [Learn Foundation](./base/foundation.md) | [Symbols](../reference/cpp/base/foundation.md) |
| Results | Expected values, absence, errors, and exception boundaries | [Learn Results](./base/exceptions-results.md) | [Symbols](../reference/cpp/base/results.md) |
| Meta and hashing | Type/symbol identity, traits, FNV, CRC, and checksums | [Learn Meta/Hashing](./base/meta-hashing.md) | [Symbols](../reference/cpp/base/meta-hashing.md) |
| Utilities | Type erasure, callables, interning, symbols, and shared helpers | [Learn Utilities](./base/utilities.md) | [Symbols](../reference/cpp/base/utilities.md) |
| Text | Owned text and Unicode operations | [Learn Text](./base/text.md) | [Symbols](../reference/cpp/base/text.md) |
| Math and units | Vectors, matrices, geometry, quantities, and ratios | [Learn Math](./base/math-units.md) | [Symbols](../reference/cpp/base/math-units.md) |
| Time | Monotonic clocks, time points, durations, and sleep | [Learn Time](./base/time.md) | [Symbols](../reference/cpp/base/time.md) |
| SIMD | Explicit vector operations and backend selection | [Learn SIMD](./base/simd.md) | [Symbols](../reference/cpp/base/simd.md) |

The broader [Async and execution](./base/async-execution.md), [memory and
containers](./base/memory-containers.md), [I/O and networking](./base/io-networking.md),
and [text, math, and time](./base/text-math-time.md) pages explain how related
subsystems fit together. They are maps, not substitutes for the focused
learning paths above.

## Link targets

NGIN.Base exposes aggregate and component targets:

```cmake
target_link_libraries(MyTarget PRIVATE NGIN::Base)
target_link_libraries(MyNetworkTarget PRIVATE NGIN::Base::Net)
```

The component families are `Foundation`, `Execution`, `IO`, `Serialization`,
`Crypto`, `Net`, and `NetTLS`, each with `Static`, `Shared`, and preferred-form
targets where that form is available.

## Design boundary

NGIN.Base does not install a hidden global scheduler or require the NGIN.Core
application host. Applications choose and own execution runtimes explicitly.

> [!WARNING]
> NGIN.Base is experimental. Prefer documented central APIs and avoid depending
> on implementation-detail namespaces.
