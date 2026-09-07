---
title: Learn NGIN.Networking
description: Choose and use NGIN.Base addresses, resolution, sockets, coroutine networking, transports, and TLS.
---

# Learn NGIN.Networking

`NGIN::Net` is a low-level non-blocking socket library with a shared I/O runtime.

## Start here

1. Learn [addresses, endpoints, and resolution](./networking/addresses-resolution.md).
2. Choose [manual non-blocking sockets](./networking/manual-sockets.md) when
   your loop owns readiness and retry.
3. Choose [coroutine sockets and the network driver](./networking/async-driver.md)
   when `TaskContext` owns cancellation and resumption.
4. Build protocols with [transports and framing](./networking/transports-framing.md).
5. Add [TLS](./networking/tls.md) with explicit verification policy.
6. Look up exact declarations in the [Networking API reference](/reference/doxygen/base/namespaceNGIN_1_1Net.html).

The most important lifetime chain is:

```text
IO::Runtime → socket operation → TaskContext/cancellation → caller buffer
```

Every item on the left and the caller buffer must satisfy the operation’s
documented lifetime until completion.

Use it when you want:

- direct control over TCP or UDP sockets
- non-blocking `Try*` calls in your own loop
- coroutine-based async socket operations without hidden runtime threads
- transport adapters for byte streams or framed messages

The two main styles are:

- manual non-blocking:
  - open sockets
  - call `Try*`
  - handle `WouldBlock`
  - integrate with your own readiness loop
- coroutine async:
  - create a `NGIN::IO::Runtime`
  - run or poll it
  - use `ConnectAsync`, `AcceptAsync`, `SendAsync`, and `ReceiveAsync`

## When To Use It

Use `NGIN::Net` when:

- you want explicit control over socket IO
- you already use the `NGIN::Async` task model
- you need a transport layer that stays close to the underlying socket behavior

## When Not To Use It

You probably do not need it when:

- a higher-level framework already owns networking in your application
- you want a batteries-included networking stack with its own hidden runtime
- you do not need non-blocking or coroutine-based networking

## Stability

- Stable and central:
  - low-level TCP/UDP socket wrappers
  - explicit `NGIN::IO::Runtime`
  - `WouldBlock`-based non-blocking flow
- Usable and maturing:
  - higher-level transport guidance and end-to-end examples
  - some builder/adaptor ergonomics

## Which API Should I Use?

- Need raw TCP control:
  - use `TcpSocket`
- Need to accept incoming TCP connections:
  - use `TcpListener`
- Need UDP datagrams:
  - use `UdpSocket`
- Need coroutine-based socket async:
  - use a `NGIN::IO::Runtime` plus the async socket methods
- Need to parse a numeric address or endpoint:
  - use `IpAddress::Parse` or `Endpoint::Parse`
- Need to resolve a hostname or service:
  - use `Resolve` synchronously or `ResolveAsync` with an owned `ResolverDriver`
- Need byte-stream semantics on top of TCP:
  - use `TcpByteStream`
- Need framed messages:
  - use `LengthPrefixedMessageStream`
- Need authenticated and encrypted byte-stream transport:
  - create a client or server `TLS::TlsContext`, then wrap an `IByteStream` in
    `TLS::TlsStream`

## Most Important Rule

Sockets are non-blocking by default.

That means:

- `Try*` methods may return `NetErrorCode::WouldBlock`
- `WouldBlock` means “not ready yet”, not “fatal error”
- async socket operations require a `NGIN::IO::Runtime`

The runtime polls networking on an owned thread by default. In Manual mode,
call `Run()` or `PollOnce()` so pending socket operations can progress.

## Addresses and endpoints

`IpAddress::Parse` accepts strict decimal IPv4 and RFC-style IPv6 text. IPv4
components with leading zeroes are rejected so they cannot be mistaken for
octal notation. IPv6 formatting follows the usual lowercase, longest-zero-run
canonical form. IPv4-mapped IPv6 addresses format as
`::ffff:192.0.2.1`.

`Endpoint::Parse` accepts `192.0.2.1:443` for IPv4 and `[2001:db8::1]:443`
for IPv6. IPv6 endpoints must be bracketed. A numeric IPv6 scope identifier is
written inside the brackets, for example `[fe80::1%7]:443`, and is propagated
to the native socket scope field. Interface-name scopes are intentionally not
accepted because resolving names to indices is an operating-system lookup.

Both values provide allocation-free `TryFormat`, allocating `ToString`, value
comparison, and explicit hash functors.

## Name resolution

`Resolve` wraps the platform resolver and returns endpoints plus socket type,
protocol, and canonical-name metadata when requested. Family and socket-type
filters are explicit. Numeric host and service flags make offline validation
deterministic. Result order is the platform resolver's order and is not a
portable sorting guarantee; duplicate equivalent records are removed while
preserving the first occurrence.

`ResolveError` retains the mapped `NetError`, the original resolver status,
and its diagnostic text. Synchronous resolution cannot portably interrupt an
in-flight `getaddrinfo` call; a positive timeout in `ResolveOptions` is honored
by `ResolveAsync`.

`ResolveAsync` requires both a caller `TaskContext` and an explicitly owned
`ResolverDriver`. Blocking resolver calls run on the driver's worker pool and
completion resumes on the caller executor. Cancellation and timeout can return
before the operating-system lookup finishes; the driver remains responsible
for its worker until that lookup exits. There is no global resolver pool.

## Smallest Useful Examples

### Manual non-blocking TCP client

```cpp
NGIN::Net::TcpSocket socket;

auto opened = socket.Open();
if (!opened)
{
    return;
}

auto connect = socket.TryConnect(
    {NGIN::Net::IpAddress::LoopbackV4(), 9000});

if (!connect)
{
    if (connect.error().code == NGIN::Net::NetErrorCode::WouldBlock)
    {
        // wait for writability in your own loop, then try again
        return;
    }

    return;
}
```

Use this style when you already have your own event loop or readiness model.

### Coroutine-based TCP client

```cpp
NGIN::Execution::ThreadPoolScheduler scheduler(1);
NGIN::IO::Runtime io;
NGIN::Async::TaskContext ctx(scheduler);
NGIN::Net::TcpSocket socket(io);
if (!socket.Open()) return;

// At the application boundary; do not block a task worker with SyncWait.
auto result = NGIN::Async::SyncWait(ctx, socket.ConnectAsync(
        ctx, {NGIN::Net::IpAddress::LoopbackV4(), 9000}));
if (!result.Succeeded()) return;
```

Use this style when the rest of your code already uses `Task<T, E>`.

## Common Workflows

### Use `Try*` when you own the loop

The manual path is:

1. call `Open`
2. call a `Try*` method
3. if it succeeds, continue
4. if it returns `WouldBlock`, wait for readiness and try again
5. if it returns another error, handle failure

This applies to:

- `TryConnect`
- `TryAccept`
- `TrySend`
- `TryReceive`

### Bind a runtime before using async methods

The coroutine path is:

1. create a `NGIN::IO::Runtime`
2. construct sockets/listeners with that runtime (the default mode polls in the background)
3. create a `TaskContext`
4. call the async socket methods from tasks

Examples:

- `TcpSocket::ConnectAsync`
- `TcpSocket::SendAsync`
- `TcpSocket::ReceiveAsync`
- `TcpListener::AcceptAsync`

### Accept incoming TCP connections

Use `TcpListener` for a listening socket.

Manual flow:

- `Open`
- `Bind`
- `Listen`
- `TryAccept`

Async flow:

- same setup
- then `AcceptAsync(ctx)`

### Use transport adapters at the right level

Use `TcpSocket` directly when you want raw socket control.

Use `TcpByteStream` when you want async read/write stream semantics.

Use `LengthPrefixedMessageStream` when your protocol is message-oriented and every message has a 32-bit big-endian
length prefix.

## Error Handling

There are two distinct styles:

### Manual non-blocking style

Use `NetExpected<T>` and branch on `NetErrorCode`.

Most importantly:

- `WouldBlock` means try again after readiness
- it is not the same thing as connection failure or EOF
- `ResourceExhausted` means the async readiness or cancellation state could
  not be allocated; the operation was not registered

### Coroutine async style

Use `Task<T, NetError>`.

At the root of the program or in tests:

- `Completion<T, NetError>::IsDomainError()` means a networking-domain failure
- `IsCanceled()` means cancellation
- `IsFault()` means async/runtime failure

Cancellation is not reported as `NetError`.

### TLS streams

TLS is a provider-neutral filter over `IByteStream`. Include
`<NGIN/NetTLS.hpp>` and link `NGIN::Base::NetTLS`; plaintext networking remains
available through `<NGIN/Net.hpp>` and `NGIN::Base::Net` without a Crypto
dependency. The OpenSSL provider is
enabled with `NGIN_BASE_TLS_WITH_OPENSSL=ON`; a configuration that must have it
can additionally set `NGIN_BASE_TLS_REQUIRE_PROVIDER=openssl`.

Client peer and hostname verification are required by default. Trust can come
from system roots, an explicit certificate collection, or both. Server
contexts require certificate-chain and private-key material and can optionally
require client authentication. SNI, hostname/IP verification, TLS 1.2/1.3,
ALPN server preference, clean shutdown, fragmented records, operation
cancellation, and handshake deadlines are part of the public contract.

Use `ReadTlsAsync` and `WriteTlsAsync` when the caller needs the full
`TlsError` taxonomy. The inherited `IByteStream` methods map failures to the
coarser `NetError` domain for generic transport consumers. Only one read, one
write, and one control operation may be active at a time; overlapping
operations fail explicitly.

If no provider was compiled, context and stream factories return
`TlsErrorCode::ProviderUnavailable`. They never silently fall back to plaintext.

## Shared I/O runtime

Bind sockets and filesystems to the same `NGIN::IO::Runtime`. It lazily creates
independent file and socket backends. Accepted sockets and transport adapters
retain their runtime binding. TaskContext selects the continuation executor;
async socket methods also inherit its cancellation token.

The default Background mode owns its polling thread. Manual mode supports
`Run()` or nonblocking `PollOnce()` on one externally owned polling thread.
Cancel and await pending work before `Stop()`, which ends polling and rejects
new async work. The runtime must outlive resources and operations.

## Common Mistakes

- Using async socket methods without a bound runtime, or neglecting to poll Manual mode.
- Treating `WouldBlock` as a fatal error.
- Reaching for transport adapters when raw sockets are the right level.
- Reaching for raw sockets when a byte-stream or message-stream adapter is the right level.
- Mixing manual non-blocking flow and coroutine flow without being clear which side owns readiness.

## Platform Notes

- Windows uses IOCP-backed async operations.
- Linux uses epoll, macOS uses kqueue, and other POSIX targets use the select fallback.
- All platforms expose non-blocking `Try*` APIs.

Platform differences should not change the basic usage model:

- manual non-blocking flow uses `Try*`
- coroutine async flow uses `NGIN::IO::Runtime`

## Reference Notes

Important types:

- `TcpSocket`
- `TcpListener`
- `UdpSocket`
- `NGIN::IO::Runtime`
- `TcpByteStream`
- `LengthPrefixedMessageStream`

The public headers and tests are the source of truth for behavior not covered
by this guide.
