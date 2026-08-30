---
title: NGIN.Base Networking and TLS API
description: Addresses, endpoints, resolution, non-blocking sockets, network drivers, transports, framing, and TLS streams.
---

# NGIN.Base Networking and TLS API

**Includes:** `<NGIN/Net.hpp>`, `<NGIN/NetTLS.hpp>`  
**Targets:** `NGIN::Base::Net`, `NGIN::Base::NetTLS`  
**Namespaces:** `NGIN::Net`, `NGIN::Net::TLS`

NGIN.Net is a low-level non-blocking socket library. Choose manual `Try*`
operations when your application owns readiness, or async methods when an
explicit `NetworkDriver` owns readiness.

## API map

| Need | Type |
| --- | --- |
| IP value | `IpAddress` |
| Address plus port | `Endpoint` |
| Host/service lookup | `Resolve`, `ResolveAsync`, `ResolverDriver` |
| Outbound TCP | `TcpSocket` |
| Inbound TCP | `TcpListener` |
| UDP | `UdpSocket` |
| Async readiness | `NetworkDriver` |
| Byte-stream abstraction | `IByteStream`, `TcpByteStream` |
| Datagram abstraction | `IDatagramChannel`, `UdpDatagramChannel` |
| Framed messages | `LengthPrefixedMessageStream` |
| TLS configuration | `TLS::TlsContext` |
| Encrypted stream | `TLS::TlsStream` |

## Errors

Socket and transport operations use `NetError`. `NetErrorCode::WouldBlock` is
not a fatal connection failure: the non-blocking operation is not ready. Wait
for the required readiness and try again, or use the driver's async operation.
Preserve the native error value in diagnostics.

## Addresses and endpoints

```cpp
auto address = NGIN::Net::IpAddress::Parse("127.0.0.1");
auto endpoint = NGIN::Net::Endpoint::Parse("[::1]:9000");
```

- IPv4 parsing is strict decimal; leading-zero components are rejected.
- IPv6 endpoints must be bracketed.
- Numeric IPv6 scopes use `[fe80::1%7]:443`.
- `TryFormat` writes into caller storage; `ToString` allocates.

Parsing accepts numeric values only. Use `Resolve` for hostnames and services.

## Name resolution

`Resolve` returns endpoints plus socket type, protocol, and optional canonical
name metadata. Result order follows the platform resolver and is not a portable
priority guarantee. Equivalent duplicates are removed while preserving the
first result.

`ResolveAsync` needs a caller `TaskContext` and owned `ResolverDriver`. A
timeout or cancellation can complete the caller before the blocking platform
lookup exits; the driver must remain alive until its worker returns. There is
no global resolver pool.

## Manual non-blocking TCP

```cpp
NGIN::Net::TcpSocket socket;
auto opened = socket.Open();
if (!opened) return Report(opened.error());

auto connected = socket.TryConnect(
    {NGIN::Net::IpAddress::LoopbackV4(), 9000});

if (!connected &&
    connected.error().code == NGIN::Net::NetErrorCode::WouldBlock) {
    WaitForWritability(socket);
    connected = socket.TryConnect(
        {NGIN::Net::IpAddress::LoopbackV4(), 9000});
}
```

Apply the same pattern to `TryAccept`, `TrySend`, and `TryReceive`. Successful
send and receive operations can be partial; keep the remaining span and retry.

## Coroutine sockets

```cpp
NGIN::Execution::CooperativeScheduler scheduler;
NGIN::Async::TaskContext context {scheduler};
auto driver = NGIN::Net::NetworkDriver::Create({});

NGIN::Net::TcpSocket socket;
if (!socket.Open()) return;

auto task = [&]() -> NGIN::Async::Task<void, NGIN::Net::NetError> {
    co_await socket.ConnectAsync(
        context,
        *driver,
        {NGIN::Net::IpAddress::LoopbackV4(), 9000},
        context.GetCancellationToken());
    co_return;
}();

auto operation = NGIN::Async::Spawn(context, std::move(task));
while (!operation.IsCompleted()) {
    driver->PollOnce();
    scheduler.RunUntilIdle();
}
```

The network driver handles readiness; the task context chooses where the
coroutine resumes. Both owners, the socket, and the buffers used by the
operation must remain valid until completion.

## Transports and framing

`TcpByteStream` adapts a TCP socket to `IByteStream`.
`UdpDatagramChannel` adapts UDP to `IDatagramChannel`. Builders compose
transport adapters explicitly.

`LengthPrefixedMessageStream` adds message boundaries to a byte stream. Set
and enforce a maximum frame length before allocating payload storage. A valid
length prefix is not proof that the peer is trusted.

## TLS

TLS is a separate component because it depends on Crypto. Create an explicit
client or server `TlsContext`, configure credentials and verification policy,
then wrap an owned/borrowed byte stream in `TlsStream` according to the
constructor's ownership contract.

TLS errors are represented by `TlsError`; authentication, handshake,
configuration, provider, closure, and transport failures must not be treated as
interchangeable. Client code should configure peer and hostname verification
for production. A successful encrypted handshake without the required identity
check is not sufficient authentication.

## Common failures

| Symptom | Cause | Fix |
| --- | --- | --- |
| Async connect never completes | `NetworkDriver` is not polled/run | Drive the network driver and resume executor |
| `WouldBlock` treated as disconnect | Manual non-blocking flow is incomplete | Wait for readiness and retry |
| Corrupt application messages | TCP byte chunks treated as message boundaries | Add an explicit framing protocol |
| TLS connects to the wrong peer | Identity verification is disabled/misconfigured | Set trust and expected-host policy before handshake |

**Source:** [`NGIN/Net`](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Net), [`NGIN/NetTLS.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/NetTLS.hpp)

