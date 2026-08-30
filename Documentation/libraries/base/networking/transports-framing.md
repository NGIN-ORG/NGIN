---
title: Transports and framing
description: Lift sockets into byte streams or datagram channels and add bounded message framing at the correct protocol layer.
---

# Transports and framing

Use raw sockets for socket options/readiness control. Use transport interfaces
when protocol code needs a narrower abstraction:

| Abstraction | Contract |
| --- | --- |
| `IByteStream` | Ordered async bytes without message boundaries |
| `TcpByteStream` | TCP implementation of byte-stream operations |
| `IDatagramChannel` | Async discrete datagrams plus source endpoint |
| `UdpDatagramChannel` | UDP implementation of datagram channel |
| `LengthPrefixedMessageStream` | 32-bit big-endian bounded messages over a byte stream |

`ByteStreamBuilder` consumes/configures a socket and returns either an
`IByteStream` or length-prefixed wrapper. `DatagramBuilder` builds an
`IDatagramChannel`. Build failures use `NetExpected` and leave no hidden global
runtime.

Byte-stream reads may be partial. Message framing must buffer prefix/payload,
enforce maximum length before allocation, and treat truncated EOF distinctly.
Do not assume one TCP write equals one receive.

Transport objects borrow/use the explicit driver/context for operations and
own or wrap their inner socket/stream according to factory signatures. Keep
the entire chain alive through pending calls; avoid overlapping operations not
permitted by the interface.

