---
title: Network TLS API
description: API reference for TLS contexts, streams, protocol options, verification, and peer state.
---

# Network TLS API

**Headers:** `<NGIN/Net/TLS.hpp>`, `<NGIN/NetTLS.hpp>`

`TlsContext::CreateClient(TlsClientContextOptions)` and
`CreateServer(TlsServerContextOptions)` return `TlsExpected<TlsContext>`.
`IsValid`, `IsClient`, and `ProviderName` inspect a context.
`TlsProviderAvailable()` reports whether a compiled provider can create one.

Options separate protocol versions, trust material, peer verification, client
authentication, certificate credentials, and application protocols. Central
enums are `TlsProtocolVersion`, `TlsPeerVerification`,
`TlsClientAuthentication`, and `TlsStreamState`.

`TlsStream::CreateClient`/`CreateServer` wrap an owned `IByteStream`.
`HandshakeAsync`, `ReadTlsAsync`, `WriteTlsAsync`, and `ShutdownAsync` preserve
TLS-specific errors. The `IByteStream` overrides adapt read/write failures to
`NetError` for transport composition.

After handshake, `NegotiatedProtocol`, `ServerName`, and `PeerCertificate`
expose negotiated state. A parsed peer certificate is not sufficient evidence
of identity: configure trust and hostname policy in client options and treat
verification failure as terminal. Never fall back to plaintext on a TLS error.

**Defined:** [`TlsContext.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Net/TLS/TlsContext.hpp), [`TlsStream.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Net/TLS/TlsStream.hpp)
