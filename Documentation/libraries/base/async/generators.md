---
title: Async generators
description: Produce and consume asynchronous sequences one value at a time with cancellation and typed errors.
---

# Async generators

`AsyncGenerator<T, E>` is a pull-based asynchronous sequence. The producer
uses `co_yield`; the consumer calls `Next(context)` for one item at a time.

## Produce values

```cpp
NGIN::Async::AsyncGenerator<int, ReadError>
ReadNumbers() {
    co_yield 10;
    co_yield 20;
    co_yield 30;
}
```

Creating the generator does not eagerly produce every value. Each `Next`
advances the producer until it yields, ends, or fails.

## Consume values

Consumption happens inside a task:

```cpp
NGIN::Async::Task<int, ReadError>
Sum(NGIN::Async::TaskContext& context) {
    auto numbers = ReadNumbers();
    int total = 0;

    for (;;) {
        NGIN::Async::GeneratorNext<int> next =
            co_await numbers.Next(context);

        if (next.IsEnd()) {
            break;
        }

        total += next.Value();
    }

    co_return total;
}
```

Check `HasItem()`/`IsEnd()` before `Value()`. Dereference and arrow operators
have the same item-present precondition.

## Errors and cancellation

`Next(context)` returns `Task<GeneratorNext<T>, E>`. Producer domain error,
cancellation, and fault propagate through that task exactly like other awaits.
The context token can cancel a pending advance.

## Single consumer

Do not call `Next` concurrently. The generator stores one consumer
continuation. Concurrent consumers terminate with an
`InvalidContinuationState` fault rather than turning the generator into a
broadcast stream.

If several consumers need the values, put an explicitly synchronized queue or
channel between one generator consumer and the downstream workers.

## Value lifetime

`GeneratorNext<T>` owns an optional `T`, so its item remains valid while that
result object remains alive and unmoved. References inside `T` still obey their
own source lifetimes.

## When to use a generator

Use it for paged reads, streamed records, progress samples, or any operation
where one async source produces a sequence and the consumer controls demand.
Use `Task<Vector<T>, E>` when the whole collection is naturally produced and
needed at once.

Return to [Learn NGIN.Async](../async.md), or inspect
[`AsyncGenerator<T, E>`](../../../reference/cpp/base/async/async-generator.md).

