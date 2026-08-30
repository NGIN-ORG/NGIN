---
title: Choosing a result shape
description: Distinguish values, absence, recoverable failure, cancellation, and exceptions.
---

# Choosing a result shape

Choose the return shape from the states the caller must handle.

| Outcome set | Shape |
| --- | --- |
| Always a value or contract violation | `T` |
| Value or ordinary absence | `Optional<T>` |
| Value or actionable failure | `Expected<T, E>` |
| Deferred value/error/cancellation | `Async::Task<T, E>` |
| Cannot continue through the current contract | documented exception |

Use `Optional` for questions such as “is this key present?” when missing is not
an error. If missing, malformed, permission denied, and unavailable require
different caller behavior, use `Expected` with a domain error.

An empty value and an error are not substitutes. Returning an empty vector on
I/O failure makes a real empty file indistinguishable from a failed read.

## Domain errors

Prefer a focused error enum/value near the subsystem that owns the operation.
Include portable classification and only the context callers may safely use.
Platform codes, paths, endpoints, and provider diagnostics can be useful, but
secret data and internal implementation details should not leak.

## Asynchronous work

An async task adds two axes: execution is deferred, and cancellation can be
distinct from the domain error. Preserve all three terminal outcomes—value,
error, cancelled—at the owner. Do not translate cancellation into an arbitrary
“failed” error unless the public domain contract explicitly does so.

The shape is part of the API contract. Changing from `Optional` to `Expected`,
or collapsing typed errors into a string, changes what callers can prove.
