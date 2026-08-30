---
title: I/O and networking
description: Use paths, files, processes, sockets, asynchronous networking, and TLS streams.
---

# I/O and networking

NGIN.Base groups filesystem and process operations under I/O, and transport
operations under networking.

## I/O

The I/O area covers paths, files, directories, streams, and process execution.
Prefer path-aware APIs over manual string concatenation, and preserve native
errors when reporting failed operations.

## Networking

The Net component covers sockets, endpoints, async operations, and transport
composition. `NetTLS` adds TLS streams through a configured cryptographic
provider.

```text
application protocol
        │
     TLS stream       optional
        │
   network stream
        │
      socket
```

## Runtime ownership

Async network operations require an execution owner. Define how pending work is
cancelled and drained during shutdown; do not let socket lifetime depend on an
unowned detached task.

## Security boundary

TLS provides a secure transport primitive, not an application trust policy.
Applications still decide which peers, certificates, protocols, and failure
modes are acceptable.
