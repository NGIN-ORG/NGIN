---
title: NGIN.Async API reference
description: Symbol-level reference for NGIN.Base coroutine tasks, operations, completions, contexts, cancellation, combinators, and generators.
---

# NGIN.Async API reference

**Component target:** `NGIN::Base::Execution`  
**Umbrella:** `<NGIN/Async.hpp>`  
**Namespace:** `NGIN::Async`

This is code reference. If the types below are new to you, start with
[Learn NGIN.Async](../../../../libraries/base/async.md).

## Core task model

| Symbol | Header | Purpose |
| --- | --- | --- |
| [`Task<T, E>`](./task.md) | `<NGIN/Async/Task.hpp>` | Cold, move-only coroutine result |
| [`Operation<T, E>`](./operation.md) | `<NGIN/Async/Task.hpp>` | Started root task and one-time completion owner |
| [`Completion<T, E>`](./completion.md) | `<NGIN/Async/Completion.hpp>` | Success, domain error, cancellation, or fault |
| [`TaskContext`](./task-context.md) | `<NGIN/Async/TaskContext.hpp>` | Non-owning executor binding plus cancellation state |
| `AsyncFault` | `<NGIN/Async/AsyncFault.hpp>` | Infrastructure/runtime failure payload |
| `NoError` | `<NGIN/Async/NoError.hpp>` | Default marker when no domain error is expected |

## Starting work

| Function | Declaration summary |
| --- | --- |
| `Spawn` | `Operation<T, E> Spawn(TaskContext&, Task<T, E>&&) noexcept` |
| `Detach` | `void Detach(TaskContext&, Task<T, E>&&) noexcept` |
| `SyncWait` | `Completion<T, E> SyncWait(TaskContext&, Task<T, E>&&)` |

These functions are documented with [`Operation<T, E>`](./operation.md).

## Cancellation

| Symbol | Purpose |
| --- | --- |
| [`CancellationSource`](./cancellation.md#cancellationsource) | Own and request cancellation state |
| [`CancellationToken`](./cancellation.md#cancellationtoken) | Observe/register for cancellation |
| [`CancellationRegistration`](./cancellation.md#cancellationregistration) | Scope registration lifetime |
| `CancellationRegistrationError` | Report registration resource exhaustion |

## Composition and sequences

| Symbol | Header | Purpose |
| --- | --- | --- |
| [`WhenAll`](./combinators.md#whenall) | `<NGIN/Async/WhenAll.hpp>` | Consume tasks and await all |
| [`WhenAny`](./combinators.md#whenany) | `<NGIN/Async/WhenAny.hpp>` | Race factories with linked child cancellation |
| [`AsyncGenerator<T, E>`](./async-generator.md) | `<NGIN/Async/AsyncGenerator.hpp>` | Pull-based asynchronous sequence |
| `GeneratorNext<T>` | `<NGIN/Async/AsyncGenerator.hpp>` | One yielded item or end marker |

## Terminal awaiters

| Function | Use in |
| --- | --- |
| `DomainFailure(E)` | `Task<void, E>` coroutine |
| `Canceled()` | A void task that terminates as canceled |
| `Faulted(AsyncFault)` | A void task that terminates with runtime fault |

## Source tree

[`Dependencies/NGIN/NGIN.Base/include/NGIN/Async`](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Async)
contains the installed public declarations. Lowercase `detail` declarations
are implementation details and are not listed as public API.

