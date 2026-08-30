---
title: NGIN.Networking API
description: Code-grounded index of addresses, resolution, sockets, runtime, transports, framing, and TLS.
---

# NGIN.Networking API

**Headers:** `<NGIN/Net.hpp>`, `<NGIN/NetTLS.hpp>`  
**Namespace:** `NGIN::Net`  
**Targets:** `NGIN::Base::Net`, `NGIN::Base::NetTLS`

| Area | Central declarations | Reference |
| --- | --- | --- |
| Addressing and resolution | `IpAddress`, `Endpoint`, `Resolve`, `ResolveAsync` | [Addresses and resolution](./networking/addresses-resolution.md) |
| Sockets | `SocketHandle`, `TcpSocket`, `TcpListener`, `UdpSocket` | [Sockets](./networking/sockets.md) |
| Coroutine runtime | `NetworkDriver`, async socket methods | [NetworkDriver](./networking/network-driver.md) |
| Transport and framing | `IByteStream`, builders, `LengthPrefixedMessageStream` | [Transport](./networking/transport.md) |
| TLS | `TlsContext`, `TlsStream`, verification/options | [TLS](./networking/tls.md) |

`NetExpected<T>` carries `NetError`. Manual non-blocking operations may return
`WouldBlock`; coroutine operations require a progressing `NetworkDriver` and
retain caller buffers until completion.

[Learn networking from the beginning](../../../libraries/base/networking.md).  
[Browse all Net headers](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Net).
