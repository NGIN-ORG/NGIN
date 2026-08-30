---
title: Manual non-blocking sockets
description: Drive TCP listeners/connections and UDP datagrams through Try operations, readiness, partial transfer, and WouldBlock retry.
---

# Manual non-blocking sockets

Sockets are non-blocking by default. The manual TCP client flow is:

```cpp
NGIN::Net::TcpSocket socket;
if (auto opened = socket.Open(); !opened) {
    return Report(opened.error());
}

auto connected = socket.TryConnect(endpoint);
if (!connected &&
    connected.error().code == NGIN::Net::NetErrorCode::WouldBlock) {
    RegisterWritable(socket.Handle());
    return;
}
```

After readiness, retry/complete according to the socket method contract.
`WouldBlock` means no progress now, not EOF or disconnection.

`TrySend`/`TryReceive` can transfer fewer bytes than requested. Maintain buffer
offsets and retry only after readiness. A zero receive and an error have
different connection semantics; use the documented return.

For servers: `TcpListener::Open`, `Bind`, `Listen`, then `TryAccept` after
readability. For UDP: open/bind as needed and use datagram send/receive methods;
one successful receive retains one datagram boundary.

`SocketHandle` owns the native socket and is invalid after close/move. Options,
shutdown direction, and endpoint queries return `NetExpected` failures. Do not
share manual operations concurrently without an explicit per-socket protocol.

