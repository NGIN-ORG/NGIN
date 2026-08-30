---
title: Network socket API
description: API reference for TCP sockets/listeners, UDP datagrams, handles, options, and non-blocking results.
---

# Network socket API

`SocketHandle` is the move-only native resource owner. `TcpSocket`,
`TcpListener`, and `UdpSocket` wrap it with protocol operations and expose
`Handle()` when low-level option access is required.

`TcpSocket` supports creation, blocking/non-blocking mode, connect, send,
receive, shutdown, close, endpoints, and socket options. Send/receive counts may
be smaller than the supplied span; zero and `WouldBlock` have operation-specific
meaning and must not be conflated.

`TcpListener` binds/listens and accepts `TcpSocket` values. `UdpSocket` binds,
sends to an endpoint, and receives a `DatagramReceiveResult` containing byte
count and remote endpoint. UDP preserves datagram boundaries; truncation policy
depends on supplied buffer and platform result.

Async methods add `TaskContext`, `NetworkDriver`, and `CancellationToken`:
`ConnectAsync`, `SendAsync`, `ReceiveAsync`, `AcceptAsync`, `SendToAsync`, and
`ReceiveFromAsync`. Buffers remain borrowed until the returned task completes.

Portable failures are `NetError` values. `WouldBlock` is an expected readiness
state for a non-blocking socket, not peer closure.

[Browse socket declarations](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Net/Sockets).
