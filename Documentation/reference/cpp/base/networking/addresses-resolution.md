---
title: Network addresses and resolution API
description: API reference for IpAddress, Endpoint, resolver options, and sync/async name resolution.
---

# Network addresses and resolution API

## Value types

`IpAddress` stores IPv4 or IPv6 bytes and reports its `AddressFamily`.
Factories include any and loopback addresses. `Parse`, `TryFormat`, and
`ToString` convert textual forms. `IpAddressHash` supports hash containers.

`Endpoint` combines an address and port. Its `Parse`, `TryFormat`, `ToString`,
comparison, and `EndpointHash` operations are value-based. Parsing returns
`AddressExpected<T>` with an `AddressParseError` and code.

## Resolution

**Header:** `<NGIN/Net/Resolve.hpp>`

```cpp
ResolveExpected<std::vector<ResolvedAddress>> Resolve(
    std::string_view host, std::string_view service,
    const ResolveOptions& options = {});

Async::Task<std::vector<ResolvedAddress>, ResolveError> ResolveAsync(
    Async::TaskContext&, std::string host, std::string service,
    ResolveOptions options = {});
```

`ResolveOptions` selects address family, socket type, protocol/hints, and result
policy. Each `ResolvedAddress` contains the resolved endpoint plus associated
socket metadata. Async resolution owns its host/service strings for the task;
the task still follows normal cold-task and context-lifetime rules.

**Defined:** [`IpAddress.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Net/Types/IpAddress.hpp), [`Resolve.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Net/Resolve.hpp)
