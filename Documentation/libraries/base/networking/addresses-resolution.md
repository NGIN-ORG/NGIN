---
title: Addresses, endpoints, and resolution
description: Parse and format numeric IP endpoints and resolve host/service names with explicit family, socket, timeout, and ownership policy.
---

# Addresses, endpoints, and resolution

`IpAddress` is a numeric IPv4/IPv6 value. Parsing is strict: IPv4 leading-zero
components are rejected; IPv6 follows RFC-style text with numeric scope IDs.

`Endpoint` combines address, port, and scope. IPv6 endpoint text must be
bracketed: `[2001:db8::1]:443` or `[fe80::1%7]:443`. `TryFormat` writes caller
storage without allocation; `ToString` allocates. Explicit hash functors match
value equality.

## Resolve names

`Resolve(host, service, options)` returns `ResolvedAddress` entries containing
endpoint plus socket/protocol/canonical metadata. `ResolveOptions` filters
address family/socket type and controls numeric-only, canonical-name, and
timeout behavior.

Platform resolver order is preserved after equivalent duplicate removal; do
not treat it as portable preference sorting.

`ResolveAsync(context, driver, ...)` uses an explicitly owned `ResolverDriver`.
Blocking platform lookup runs on its workers and completion resumes through the
caller context. Cancellation/timeout may return before the OS call exits, so
the driver must remain alive until its workers finish.

`ResolveError` retains mapped `NetError`, original resolver status, and text.
Log all three where diagnostics matter.

