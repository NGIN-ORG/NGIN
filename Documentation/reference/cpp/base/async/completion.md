---
title: Completion<T, E>
description: Code reference for NGIN.Async terminal success, domain-error, canceled, and fault outcomes.
---

# `Completion<T, E>`

**Header:** `<NGIN/Async/Completion.hpp>`  
**Namespace:** `NGIN::Async`  
**Target:** `NGIN::Base::Execution`  
**Defined:** [`Completion.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Async/Completion.hpp#L56)

## Declaration

```cpp
template<typename T, typename E>
class Completion;

template<typename E>
class Completion<void, E>;
```

`Completion` is a value-owned terminal outcome. It contains exactly one of a
success value, recoverable domain error, canceled state, or `AsyncFault`.

## Outcome enums

```cpp
enum class CompletionKind : NGIN::UInt8 {
    Succeeded,
    DomainError,
    Canceled,
    Fault,
};

enum class TaskStatus : NGIN::UInt8 {
    Pending,
    Succeeded,
    DomainError,
    Canceled,
    Fault,
};
```

`Kind()` returns the outcome category. `Status()` maps that category to task
status through `ToTaskStatus`.

## Queries

```cpp
[[nodiscard]] CompletionKind Kind() const noexcept;
[[nodiscard]] TaskStatus Status() const noexcept;
[[nodiscard]] bool Succeeded() const noexcept;
[[nodiscard]] bool HasValue() const noexcept;       // non-void only
[[nodiscard]] bool IsDomainError() const noexcept;
[[nodiscard]] bool IsCanceled() const noexcept;
[[nodiscard]] bool IsFault() const noexcept;
[[nodiscard]] explicit operator bool() const noexcept;
```

The bool conversion is equivalent to success, not “contains any outcome.” A
completion is always terminal.

## Accessors

```cpp
T& Value() &;
const T& Value() const&;
T&& Value() &&;

E& DomainError() &;
const E& DomainError() const&;
E&& DomainError() &&;

AsyncFault& Fault() &;
const AsyncFault& Fault() const&;
AsyncFault&& Fault() &&;
```

| Accessor | Precondition |
| --- | --- |
| `Value` / `operator*` / `operator->` | `HasValue()` is true |
| `DomainError` | `IsDomainError()` is true |
| `Fault` | `IsFault()` is true |

The void specialization has no `Value`, dereference, or arrow member.

## Factories

```cpp
static Completion Success(T value);  // Success() for void
static Completion DomainFailure(E error);
static Completion Canceled() noexcept;
static Completion Faulted(AsyncFault fault) noexcept;
```

Move from an rvalue completion to transfer a potentially expensive value or
error:

```cpp
T value = std::move(completion).Value();
```

## Ownership and thread behavior

The completion owns its payload. Publication from a task uses release/acquire
synchronization before a root or continuation observes it. That publication
does not make arbitrary objects referenced by `T` or `E` thread-safe.

## Related symbols

- [`Task<T, E>`](./task.md)
- [`Operation<T, E>`](./operation.md)
- [Errors and completions guide](../../../../libraries/base/async/errors.md)
