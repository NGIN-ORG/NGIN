---
title: TLS streams
description: Configure provider-backed client/server TLS, verification, handshake deadlines, I/O concurrency, and clean shutdown.
---

# TLS streams

TLS is a separate `NGIN::Base::NetTLS` component because it depends on Crypto.
It wraps `IByteStream`; it never silently falls back to plaintext.

## Context and stream

```cpp
auto context = NGIN::Net::TLS::TlsContext::CreateClient(options);
if (!context) {
    return Report(context.error());
}

auto stream = NGIN::Net::TLS::TlsStream::CreateClient(
    *context, std::move(innerStream), clientOptions);
```

Client verification/hostname checking are required by default. Trust comes
from system roots, explicit certificates, or both. Server context requires a
certificate chain/private key and can require client authentication.

Call `HandshakeAsync` with `TaskContext`, cancellation token, and handshake
options/deadline. `ReadTlsAsync`, `WriteTlsAsync`, and `ShutdownAsync` expose
full `TlsError`; inherited generic byte-stream methods map to `NetError`.

Only one read, one write, and one control operation may be active at once.
Overlaps fail explicitly. Authenticate before releasing plaintext. Complete a
clean shutdown when protocol policy requires it; truncated EOF policy must be
intentional.

Inspect `State`, negotiated ALPN protocol, server name, and peer certificate
only under their documented post-handshake lifetime. If no provider is built,
factories report `ProviderUnavailable`.

