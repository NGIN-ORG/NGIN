# NGIN.Base TLS provider design review

Status: awaiting the Milestone 4 approval gate.

## Decision requested

Approve an opt-in OpenSSL 3 provider as the first TLS implementation. The
public API remains provider-neutral; native SChannel and Network.framework/
Secure Transport providers remain future additions.

No OpenSSL dependency is added to default NGIN.Base builds. Enabling
`NGIN_BASE_TLS_WITH_OPENSSL=ON`, or consuming the OpenSSL package capability,
requires both `OpenSSL::SSL` and `OpenSSL::Crypto`. An explicit
`NGIN_BASE_TLS_REQUIRE_PROVIDER=openssl` setting fails configuration if the
provider is unavailable; otherwise the provider-neutral API reports
`ProviderUnavailable` at runtime.

## Ownership and dependency direction

TLS belongs to `NGIN::Net::TLS` and composes over
`NGIN::Net::Transport::IByteStream`. It adds the already anticipated
one-way dependency `Net -> Crypto` so contexts can consume
`Crypto::Certificates::TlsCredentialMaterial`. Crypto must not depend on Net.

`TlsContext` is a cheap shared handle to immutable provider configuration.
Provider state and session objects remain private compiled types. Each
`TlsStream` exclusively owns:

- one `std::unique_ptr<IByteStream>` inner transport
- one provider session
- encrypted input/output buffers
- handshake/shutdown state

A context may create many streams concurrently. A stream itself permits one
read coroutine and one write coroutine concurrently; overlapping reads or
overlapping writes return `ConcurrentOperation`.

## Public surface

Headers under `NGIN/Net/TLS/`:

- `TlsTypes.hpp`
  - `TlsProtocolVersion`
  - `TlsPeerVerification` (`Required` or `Disabled`)
  - `TlsClientAuthentication` (`None`, `Optional`, `Required`)
  - `TlsTrustOptions` (system roots plus optional custom certificates)
  - `TlsHandshakeOptions` (timeout and cancellation are operation inputs)
- `TlsError.hpp`
  - category: transport, protocol, certificate, hostname, timeout,
    cancellation, provider, state
  - provider/native detail and diagnostic text
- `TlsContext.hpp`
  - `CreateClient(TlsClientContextOptions)`
  - `CreateServer(TlsServerContextOptions)`
  - immutable trust, credential, protocol, cipher, and ALPN configuration
- `TlsStream.hpp`
  - `CreateClient(inner, context, TlsClientOptions)`
  - `CreateServer(inner, context, TlsServerOptions)`
  - `HandshakeAsync(ctx, token)`
  - `ReadAsync`, `WriteAsync`, `ShutdownAsync`, and `Close`
  - `NegotiatedProtocol`, peer-certificate information, and state queries

Client verification defaults are secure: chain and hostname verification are
required, the verification name defaults to the connection hostname, SNI is
sent for DNS names, and disabling verification is an explicit option. Server
contexts require credentials. Client credentials are optional; server client
authentication is independently configured.

ALPN is a list of non-empty protocol byte strings of at most 255 bytes. The
server uses deterministic server-preference selection. A configured
"ALPN required" policy turns no overlap into a protocol error.

## Provider implementation

The OpenSSL implementation uses `SSL_CTX` per context and `SSL` per stream.
Memory BIOs bridge OpenSSL's synchronous state machine to asynchronous
`IByteStream` reads and writes:

1. call the relevant `SSL_*` operation
2. drain encrypted bytes from the write BIO to the inner stream
3. on `SSL_ERROR_WANT_READ`, read encrypted bytes and feed the read BIO
4. repeat until progress completes, cancellation/timeout fires, EOF occurs, or
   a structured provider error is produced

All provider calls execute on the coroutine's current executor; there is no
hidden TLS thread pool. Short writes, fragmented records, and multiple TLS
records per transport read are normal. Application writes report plaintext
bytes accepted, and reads return decrypted bytes.

`TlsCredentialMaterial` gains lossless certificate DER retention during X.509
parsing so the provider can import the exact chain. PKCS#8 private-key material
is serialized through the existing key-format writer and imported into the
provider. Custom trust certificates follow the same DER path. Key and
certificate matching is checked when the context is created.

Timeout is implemented with an operation-owned cancellation source linked to
the caller token and a scheduler deadline. The returned TLS category records
whether timeout or caller cancellation won; the inner transport still receives
cancellation through the linked token.

Clean shutdown sends and drains `close_notify`. EOF without `close_notify` is
reported as truncated TLS unless an explicit compatibility policy allows it.
`Close()` remains an immediate transport close and is safe after any state.

## Package and build changes

- add `NGIN_BASE_TLS_WITH_OPENSSL` and
  `NGIN_BASE_TLS_REQUIRE_PROVIDER`
- compile provider-neutral stubs in all builds
- compile `OpenSslTlsProvider.cpp` only with the provider enabled
- update `Packages/OpenSSL/CMakeLists.txt` to require/export `OpenSSL::SSL` and
  `OpenSSL::Crypto`
- extend `OpenSSL.nginpkg` runtime artifacts to `libssl;libcrypto` and publish
  `Net.TLS.Provider.openssl`
- add provider capability diagnostics and a TLS-provider CI job

## Verification

Tests use only loopback transports and checked-in, explicitly non-production
certificate/key fixtures. The matrix covers:

- trusted chain, untrusted issuer, expired certificate, and hostname mismatch
- SNI and ALPN success/no-overlap
- optional and required client certificates, including missing-client failure
- custom trust roots and system-root option construction
- fragmented encrypted reads/writes and short inner-stream writes
- caller cancellation and timeout during handshake
- clean `close_notify` and truncated EOF
- provider-disabled diagnostics

The provider test job enables OpenSSL 3 explicitly; default platform jobs also
exercise provider-disabled behavior.

## Rejected initial alternatives

- A raw `SSL*` public API would leak provider ownership and block future native
  providers.
- TLS directly inside `TcpSocket` would prevent composition over other byte
  streams and framing/filter order.
- Enabling OpenSSL by default would silently expand every Base consumer's
  dependency and deployment surface.
- Implementing OpenSSL, SChannel, and Apple TLS simultaneously would obscure
  the provider-neutral contract before it has one proven implementation.
