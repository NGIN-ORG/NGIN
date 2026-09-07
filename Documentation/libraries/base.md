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
4. Use the [NGIN.Base C++ reference](/reference/doxygen/base/namespaceNGIN.html) for exact headers,
   types, ownership, errors, and failure modes.

## Subsystems

| Subsystem | What it solves | Learn | C++ API |
| --- | --- | --- | --- |
| Async | Coroutine tasks, results, cancellation, composition, and async streams | [Learn Async](./base/async.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1Async.html) |
| Execution | Executors, schedulers, native threads, and fibers | [Learn Execution](./base/execution.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1Execution.html) |
| Synchronization | Locks, semaphores, guards, and atomic conditions | [Learn Sync](./base/synchronization.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1Sync.html) |
| Memory | Allocators, arenas, pools, storage, and ownership helpers | [Learn Memory](./base/memory.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1Memory.html) |
| Containers | Allocator-aware vectors, strings, and hash maps | [Learn Containers](./base/containers.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1Containers.html) |
| I/O | Paths, filesystems, file handles, processes, and libraries | [Learn I/O](./base/io.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1IO.html) |
| Networking | Addresses, sockets, coroutine I/O, transports, framing, and TLS | [Learn Networking](./base/networking.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1Net.html) |
| Serialization | JSON and XML documents, events, builders, and writers | [Learn Serialization](./base/serialization.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1Serialization.html) |
| Cryptography | Providers, secure random, keys, encryption, certificates, and tokens | [Learn Crypto](./base/cryptography.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1Crypto.html) |
| Foundation map | Provider-free shared vocabulary and navigation | [Learn Foundation](./base/foundation.md) | [Symbols](/reference/doxygen/base/namespaceNGIN.html) |
| Results | Expected values, absence, errors, and exception boundaries | [Learn Results](./base/exceptions-results.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1Utilities.html) |
| Meta and hashing | Type/symbol identity, traits, FNV, CRC, and checksums | [Learn Meta/Hashing](./base/meta-hashing.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1Meta.html) |
| Utilities | Type erasure, callables, interning, symbols, and shared helpers | [Learn Utilities](./base/utilities.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1Utilities.html) |
| Text | Owned text and Unicode operations | [Learn Text](./base/text.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1Text.html) |
| Math and units | Vectors, matrices, geometry, quantities, and ratios | [Learn Math](./base/math-units.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1Math.html) |
| Time | Monotonic clocks, time points, durations, and sleep | [Learn Time](./base/time.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1Time.html) |
| SIMD | Explicit vector operations and backend selection | [Learn SIMD](./base/simd.md) | [Symbols](/reference/doxygen/base/namespaceNGIN_1_1SIMD.html) |

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
