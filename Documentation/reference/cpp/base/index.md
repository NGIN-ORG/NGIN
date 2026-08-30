---
title: NGIN.Base C++ API
description: Public API index for NGIN.Base, organized by subsystem.
---

# NGIN.Base C++ API

**Aggregate include:** `<NGIN/NGIN.hpp>`  
**Aggregate target:** `NGIN::Base`  
**Source:** [Dependencies/NGIN/NGIN.Base/include/NGIN](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN)

Prefer a focused umbrella and component target when practical. The table below
is the entry point into the public surface; each page lists its focused headers
and central symbols.

| Subsystem | Focused include | Public surface |
| --- | --- | --- |
| [Async](./async/index.md) | `<NGIN/Async.hpp>` | `Task`, `Operation`, completion, cancellation, combinators, generators |
| [Execution](./execution.md) | `<NGIN/Execution.hpp>` | executors, schedulers, threads, and fibers |
| [Synchronization](./sync/index.md) | `<NGIN/Sync.hpp>` | mutexes, guards, spin locks, semaphores, and conditions |
| [Memory and containers](./memory-containers.md) | `<NGIN/Memory.hpp>`, `<NGIN/Containers.hpp>` | allocators, ownership helpers, vectors, hash maps |
| [I/O and processes](./io.md) | `<NGIN/IO.hpp>` | paths, filesystem handles, async I/O, processes, libraries |
| [Networking and TLS](./networking.md) | `<NGIN/Net.hpp>`, `<NGIN/NetTLS.hpp>` | addresses, sockets, drivers, transports, framing, TLS |
| [Serialization](./serialization.md) | `<NGIN/Serialization.hpp>` | JSON/XML documents, event parsers, builders, writers |
| [Cryptography](./crypto.md) | `<NGIN/Crypto.hpp>` | providers, secrets, hashes, encryption, certificates, tokens |
| [Foundation map](./foundation.md) | focused umbrellas | provider-free shared API index |
| [Results and exceptions](./results.md) | `<NGIN/Utilities.hpp>`, `<NGIN/Exceptions.hpp>` | expected values, absence, errors, exceptions |
| [Meta and hashing](./meta-hashing.md) | `<NGIN/Meta.hpp>`, `<NGIN/Hashing.hpp>` | identities, traits, FNV, CRC, checksums |
| [Utilities](./utilities.md) | `<NGIN/Utilities.hpp>` | type erasure, callables, interning, symbols |
| [Text](./text.md) | `<NGIN/Text.hpp>` | owned strings, UTF validation/conversion |
| [Math and units](./math-units.md) | `<NGIN/Math.hpp>`, `<NGIN/Units.hpp>` | linear algebra, geometry, big numbers, quantities |
| [Time](./time.md) | `<NGIN/Time.hpp>` | monotonic points and blocking sleep |
| [SIMD](./simd.md) | `<NGIN/SIMD.hpp>` | lanes, masks, policies, scans |

## Link targets

The public component families are `Foundation`, `Execution`, `IO`,
`Serialization`, `Crypto`, `Net`, and `NetTLS`. Each exposes the forms supported
by the package, such as `NGIN::Base::Net` and its static/shared variants.

## Contract conventions

- Typed `Result`/`Expected` values represent anticipated failures.
- Move-only handles normally own the underlying resource.
- Views and spans borrow; their source must outlive their use.
- Allocator-taking APIs do not transfer ownership of the allocator.
- An executor or scheduler is explicitly selected; Base installs no hidden
  application-wide runtime.
