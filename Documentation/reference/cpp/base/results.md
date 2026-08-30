---
title: Results and exceptions API
description: API reference for Expected, Unexpected, Optional, ErrorInfo, Exception, and NotSupportedException.
---

# Results and exceptions API

## Result aliases

**Headers:** `<NGIN/Utilities/Expected.hpp>`, `<NGIN/Utilities/Optional.hpp>`

```cpp
template<typename T, typename E>
using Expected = std::expected<T, E>;

template<typename E>
using Unexpected = std::unexpected<E>;

template<typename T>
using Optional = std::optional<T>;
```

All standard observers and value/error semantics apply. Exception guarantees
depend on the contained types and operation.

## `ErrorInfo`

**Header:** `<NGIN/Utilities/Error.hpp>`

`ErrorInfo` is a small cross-domain value holding `ErrorDomain`, a numeric
code, and optional native code. Prefer a subsystem-specific richer error when
callers need paths, messages, locations, provider details, or other context.

## Exceptions

**Header:** `<NGIN/Exceptions.hpp>`

`Exceptions::Exception` derives from `std::runtime_error`. Construction may
capture stack information when `NGIN_BASE_CAPTURE_EXCEPTION_STACKTRACE` is
enabled. `NotSupportedException` describes an unsupported requested operation.

**Defined:** [`Expected.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Utilities/Expected.hpp), [`Exception.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Exceptions/Exception.hpp)
