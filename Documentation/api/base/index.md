---
title: NGIN.Base API
description: Public headers, targets, namespaces, and subsystem references for NGIN.Base.
---

# NGIN.Base API

NGIN.Base is split into seven compiled ownership components. Link the narrowest
component that owns the APIs you use. Component dependencies are added
transitively.

```cmake
find_package(NGINBase CONFIG REQUIRED COMPONENTS Execution)
target_link_libraries(MyTarget PRIVATE NGIN::Base::Execution)
```

## Components

| Component | Preferred target | Umbrella | Detailed reference |
| --- | --- | --- | --- |
| Foundation | `NGIN::Base::Foundation` | `<NGIN/NGIN.hpp>` | [Foundation APIs](./foundation.md) |
| Execution | `NGIN::Base::Execution` | `<NGIN/Async.hpp>`, `<NGIN/Execution.hpp>` | [Async](./async.md), [execution and sync](./execution.md) |
| IO | `NGIN::Base::IO` | `<NGIN/IO.hpp>` | [I/O and processes](./io.md) |
| Serialization | `NGIN::Base::Serialization` | `<NGIN/Serialization.hpp>` | [Serialization](./serialization.md) |
| Crypto | `NGIN::Base::Crypto` | `<NGIN/Crypto.hpp>` | [Crypto](./crypto.md) |
| Net | `NGIN::Base::Net` | `<NGIN/Net.hpp>` | [Networking](./networking.md) |
| NetTLS | `NGIN::Base::NetTLS` | `<NGIN/NetTLS.hpp>` | [Networking and TLS](./networking.md) |

`NGIN::Base`, `NGIN::Base::Static`, and `NGIN::Base::Shared` aggregate every
component only when the complete package was built. A subset package does not
define an aggregate whose meaning changes with package contents.

## Dependency direction

```text
Foundation
   └─ Execution ─┬─ IO ─┬─ Serialization ── Crypto ─┐
                 │      └─ Net ──────────────────────┴─ NetTLS
                 └───────────────────────────────────────
```

- Foundation has no NGIN.Base dependency.
- Execution depends on Foundation.
- IO depends on Foundation and Execution.
- Serialization depends on Foundation and IO.
- Crypto depends on Foundation, IO, and Serialization.
- Net depends on Foundation, Execution, and IO.
- NetTLS depends on Net and Crypto.

## Public-header rules

- Prefer a subsystem umbrella for ordinary use and a focused header when
  compile time matters.
- A same-named umbrella owns each public module directory. For example,
  `<NGIN/Serialization/JSON.hpp>` covers `NGIN/Serialization/JSON/`.
- `<NGIN/Net.hpp>` deliberately excludes TLS. Include `<NGIN/NetTLS.hpp>` and
  link `NGIN::Base::NetTLS` for TLS.
- Lowercase `detail/` directories are not contracts.
- APIs explicitly marked experimental can change without the compatibility
  expectations of central APIs.

## Common Base-wide conventions

- C++23 is required.
- Types and methods use `PascalCase`.
- Failure-prone operations normally return an `Expected`, a subsystem result
  alias, or an async `Completion`; inspect the error before reading the value.
- Allocators, schedulers, runtimes, cancellation sources, and driver objects
  are explicit owners. NGIN.Base does not install a hidden global runtime.
- Non-owning references and views do not extend the lifetime of their source.

**Source:** [`NGIN.Base` public headers](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN)

