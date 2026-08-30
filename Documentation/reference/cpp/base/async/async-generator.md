---
title: AsyncGenerator<T, E> and GeneratorNext<T>
description: Code reference for pull-based NGIN.Async sequences and their item-or-end result type.
---

# `AsyncGenerator<T, E>` and `GeneratorNext<T>`

**Header:** `<NGIN/Async/AsyncGenerator.hpp>`  
**Namespace:** `NGIN::Async`  
**Target:** `NGIN::Base::Execution`  
**Defined:** [`AsyncGenerator.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Async/AsyncGenerator.hpp#L111)

## `AsyncGenerator<T, E>`

```cpp
template<typename T, typename E = NoError>
class AsyncGenerator final {
public:
    // move-only coroutine owner
    [[nodiscard]] Task<GeneratorNext<T>, E>
        Next(TaskContext& context);
};
```

Producer coroutines return `AsyncGenerator<T, E>` and use `co_yield T`.
`Next` resumes the producer and asynchronously returns one owned item or the
end marker. Producer domain error, cancellation, and fault propagate through
the returned task.

The generator is move-only. Destruction destroys its coroutine frame. Keep it
alive while a `Next` operation is pending.

Only one consumer may await `Next` at a time. Concurrent consumer installation
faults with `AsyncFaultCode::InvalidContinuationState`.

## `GeneratorNext<T>`

```cpp
template<typename T>
class GeneratorNext final {
public:
    static GeneratorNext Item(T value);
    static GeneratorNext End() noexcept;

    [[nodiscard]] bool HasItem() const noexcept;
    [[nodiscard]] bool IsEnd() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;

    T& Value() noexcept;
    const T& Value() const noexcept;
    T& operator*() noexcept;
    const T& operator*() const noexcept;
    T* operator->() noexcept;
    const T* operator->() const noexcept;
};
```

`Value`, dereference, and arrow require `HasItem() == true`. End-of-sequence is
a successful `GeneratorNext::End`, not a domain error.

## Cancellation and executor

The first advance binds the generator promise to the context executor. A
canceled context causes canceled `Next`. An invalid executor produces
`InvalidTaskUsage`. Cancellation registration failure becomes an async fault.

## Related guide

[Async generators tutorial](../../../../libraries/base/async/generators.md)

