---
title: Network transport and framing API
description: API reference for byte streams, datagram channels, builders, and length-prefixed messages.
---

# Network transport and framing API

`Transport::IByteStream` is the asynchronous ordered-byte contract. Its read and
write tasks use `TaskContext`, caller-owned spans, and cancellation. TCP and TLS
adapters implement the same interface. `IDatagramChannel` preserves message
boundaries and remote endpoints; `UdpDatagramChannel` is its UDP implementation.

`ByteStreamBuilder` assembles a stream and can produce either
`std::unique_ptr<IByteStream>` or a `LengthPrefixedMessageStream`.
`DatagramBuilder` performs the parallel datagram assembly. Builders return
`NetExpected` so invalid combinations fail before traffic begins.

## `LengthPrefixedMessageStream`

The filter uses a four-byte length header. `WriteMessageAsync` writes the whole
frame; `ReadMessageAsync` reads an exact header/body and returns a borrowed view
into filter-owned receive storage. A configured maximum message size prevents
unbounded allocation.

The returned message span is invalidated by the next read or filter destruction.
Framing solves byte boundaries only; it does not authenticate, compress, or
interpret payloads.

**Defined:** [`IByteStream.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Net/Transport/IByteStream.hpp), [`LengthPrefixedMessageStream.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Net/Transport/Filters/LengthPrefixedMessageStream.hpp)
