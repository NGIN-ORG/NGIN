---
title: Error and exception boundaries
description: Translate failures at subsystem, ABI, thread, and coroutine boundaries without losing evidence.
---

# Error and exception boundaries

Translate an error only when the caller crosses to a different domain. Inside
one domain, return the original typed error so code can retain its detail.

A useful translation records:

- the high-level operation that failed;
- the original portable error category;
- safe underlying platform/provider status;
- relevant path, endpoint, symbol, or option;
- a cause or structured diagnostic when the public type supports it.

Avoid replacing all failures with a string such as “operation failed.” Human
text helps logs; stable codes and fields help programs.

## Exception boundaries

`NGIN::Exceptions::Exception` derives from `std::runtime_error` and can capture
a stack trace when the build option enables it. `NotSupportedException`
represents a requested unsupported operation. Follow the throwing API's
documented contract; do not use exceptions for ordinary branching.

Catch and translate at boundaries that cannot carry an exception safely:

- a C or plugin ABI declared non-throwing;
- a thread entry function;
- a callback contract marked `noexcept`;
- a detached async owner/error sink.

Catch the narrowest known types first. Preserve `what()` as diagnostics without
treating free-form text as a machine-readable code. Unknown exceptions should
not be mislabeled as a known domain failure.

## Invariants

Programming invariants are not ordinary recoverable input failures. Assert,
terminate, throw, or return an error according to the owning contract; do not
quietly continue with a fabricated default value.
