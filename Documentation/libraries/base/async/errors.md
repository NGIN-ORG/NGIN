---
title: Async errors and completions
description: Model domain errors, cancellation, faults, success values, and one-time result consumption.
---

# Async errors and completions

NGIN.Async has one success state and three non-success states. Keeping them
separate prevents an ordinary “file not found” from looking like scheduler
corruption or user cancellation.

## Choose the task error type

The second task parameter is the recoverable domain error:

```cpp
enum class LoadError {
    NotFound,
    InvalidFormat,
};

NGIN::Async::Task<Document, LoadError> LoadDocument(...);
```

Use a domain-specific enum or value that callers can handle. Use `NoError`
(the default) only when the task has no expected failure. Do not use
`AsyncFault` as your domain type.

## Return outcomes

Success:

```cpp
co_return document;
```

Domain failure from `Task<T, E>`:

```cpp
co_return NGIN::Async::Completion<Document, LoadError>::DomainFailure(
    LoadError::NotFound);
```

Domain failure from `Task<void, E>`:

```cpp
co_await NGIN::Async::DomainFailure(LoadError::NotFound);
co_return;
```

Cancellation and fault from a void task use `Canceled()` and
`Faulted(fault)` awaiters. Ordinary application failures should remain domain
errors.

## Propagation through `co_await`

```cpp
Document document = co_await LoadDocument(context);
Index index = co_await BuildIndex(context, document);
```

If `LoadDocument` is not successful, its terminal state is copied/moved into
the parent promise. `BuildIndex` is never called. This is why a normal await
expression produces `T`, not `Completion<T, E>`.

Parent and child domain error types must be compatible with the propagation
path. When two subsystems expose different domain errors, translate them at a
root/service boundary that owns a `Completion`, or design a shared higher-level
error type. A normal child `co_await` intentionally propagates instead of
returning a completion for local inspection.

## Inspect a root completion

```cpp
auto result = operation.TakeResult();

switch (result.Kind()) {
case NGIN::Async::CompletionKind::Success:
    Use(result.Value());
    break;
case NGIN::Async::CompletionKind::DomainError:
    Handle(result.DomainError());
    break;
case NGIN::Async::CompletionKind::Canceled:
    HandleCanceled();
    break;
case NGIN::Async::CompletionKind::Fault:
    Handle(result.Fault());
    break;
}
```

Only call `Value`, `DomainError`, or `Fault` after checking the matching state.
Those are preconditions, not conversions.

## Consume an operation once

```cpp
if (operation.IsCompleted()) {
    auto result = operation.TryTakeResult();
    if (result) {
        Consume(std::move(*result));
    }
}
```

`TryTakeResult` returns empty if the operation is incomplete or its result was
already taken. `TakeResult` expects a terminal, unconsumed operation. Neither
API turns one operation into a multi-reader future.

## Faults

`AsyncFault` represents runtime/infrastructure failure. Its code can report
invalid task use, executor dispatch failure, cancellation-registration failure,
or an exception captured according to build configuration. Preserve its code
and native detail in diagnostics.

Faults are not a recovery taxonomy for your business domain. If a server
returns 404 or a parser rejects input, use the task's `E`.

## Practical boundary policy

At an application boundary:

- success continues normal flow;
- domain error becomes a user-facing or protocol-specific outcome;
- cancellation normally avoids an error alert but still performs cleanup;
- fault is logged/reported as an infrastructure failure.

Next: [cancellation](./cancellation.md) or the
[`Completion<T, E>` API reference](../../../reference/cpp/base/async/completion.md).
