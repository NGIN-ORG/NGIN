---
title: WhenAll and WhenAny
description: Code reference for NGIN.Async task combinators, constraints, return types, failure propagation, and cancellation behavior.
---

# `WhenAll` and `WhenAny`

**Headers:** `<NGIN/Async/WhenAll.hpp>`, `<NGIN/Async/WhenAny.hpp>`  
**Namespace:** `NGIN::Async`  
**Target:** `NGIN::Base::Execution`

## `WhenAll`

### Void tasks

```cpp
template<typename... TTasks>
    requires (sizeof...(TTasks) > 0) &&
             /* every argument is Task<void, E> with the same E */
[[nodiscard]] Task<void, CommonError>
WhenAll(TaskContext& context, TTasks... tasks);
```

### Value tasks

```cpp
template<typename E, typename... T>
    requires (sizeof...(T) > 0) && (!std::is_void_v<T> && ...)
[[nodiscard]] Task<std::tuple<T...>, E>
WhenAll(TaskContext& context, Task<T, E>... tasks);
```

The pack must be non-empty. Every task must use one error type. The value
overload returns values in argument order, not completion order.

`WhenAll` consumes tasks and starts each with `Spawn(context, ...)`. It awaits
all operation results. The first failure encountered during ordered result
collection is propagated after the required operations have been observed.
Cancellation already requested on `context` produces canceled completion.

**Defined:** [`WhenAll.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Async/WhenAll.hpp#L96)

## `WhenAny`

```cpp
template<typename... TFactories>
    requires (sizeof...(TFactories) > 0) &&
             /* each factory is invocable with TaskContext&,
                returns a Task, and all tasks use one E */
[[nodiscard]] Task<NGIN::UIntSize, CommonError>
WhenAny(TaskContext& context, TFactories... factories);
```

Returns the zero-based argument index of the first successfully terminal
child. If the winning child has a domain error, cancellation, or fault, that
outcome propagates instead of producing an index.

Each factory receives its own context linked to the parent. When a child wins,
the combinator requests cancellation for all losers and waits for all child
watchers before returning. Loser cancellation/drain time is observable.

The factory pack and internal shared state can allocate. This is not a
fixed-capacity, allocation-free race primitive.

**Defined:** [`WhenAny.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Async/WhenAny.hpp#L249)

## Preconditions and ownership

- Pass fresh tasks or move named tasks to `WhenAll`.
- Do not access tasks after they have been consumed.
- Factories must return tasks whose captures remain valid until drain.
- Children should observe cancellation or otherwise finish promptly.
- Parent `TaskContext` and executor must outlive the combinator.

## Related guide

[Combine async tasks](../../../../libraries/base/async/composition.md)

