---
title: Async cancellation
description: Request cancellation, propagate tokens, add timeouts, and keep cancellation lifetimes safe.
---

# Async cancellation

Cancellation is cooperative. A source records a request; tokens and
cancellation-aware suspension points observe it. The runtime does not terminate
a thread or destroy a running coroutine.

## Create and pass cancellation

```cpp
NGIN::Async::CancellationSource source;
NGIN::Async::TaskContext context {
    scheduler,
    source.GetToken(),
};

auto operation = NGIN::Async::Spawn(context, Download(context));

// Later, from the owner:
source.Cancel();
```

The source owns mutable cancellation state. A token observes it. Passing the
context through child operations keeps one cancellation lineage visible.

## Observe cancellation

`YieldNow` and `Delay` observe the context token. Tight or CPU-bound loops must
check:

```cpp
while (HasChunk()) {
    if (context.CheckCancellation()) {
        co_await NGIN::Async::Canceled();
        co_return;
    }
    HashNextChunk();
}
```

Choose a check interval that bounds cancellation latency without turning the
hot loop into mostly synchronization.

## Schedule a timeout

`CancellationSource::CancelAt` accepts an absolute monotonic `TimePoint`.
`CancelAfter` accepts a duration and an executor:

```cpp
auto scheduled = source.CancelAfter(
    scheduler,
    NGIN::Units::Seconds {5.0});

if (!scheduled) {
    HandleScheduleError(scheduled.error());
}
```

A timeout is a cancellation policy. Decide at the owning boundary whether the
user sees “timed out” as a distinct domain error or ordinary cancellation.

## Registrations

`CancellationToken::Register` attaches a callback through a
`CancellationRegistration`. Reset/destroy the registration before state it
borrows is destroyed. A callback can race normal completion, so it must be
idempotent and synchronize shared state correctly.

Registration uses the memory resource associated with the source. A custom
`std::pmr::memory_resource` must outlive the source, all copied tokens, and all
registrations. Exhaustion reports
`CancellationRegistrationError::ResourceExhausted`.

## Structured cancellation with `WhenAny`

`WhenAny` creates a distinct child context for each factory. The first terminal
child wins. The combinator then requests cancellation for every loser and
drains them before it returns.

This makes references to the parent coroutine frame safe, but it also means a
loser must cooperate:

```cpp
auto winner = co_await NGIN::Async::WhenAny(
    context,
    [](auto& child) { return ReadNetwork(child); },
    [](auto& child) { return WaitForFallback(child); });
```

If `ReadNetwork` blocks in an operation that neither observes the token nor
finishes independently, the race cannot complete promptly.

## Shutdown order

1. Stop accepting new work.
2. Request cancellation from the owner.
3. Continue driving executors/drivers.
4. Wait until operations terminate or the subsystem's hard shutdown policy is
   reached.
5. Destroy buffers/services, then contexts and schedulers.

Canceling and immediately destroying borrowed state is a use-after-free, not
cooperative shutdown.

Next: [combining tasks](./composition.md) or the
[cancellation API reference](../../../reference/cpp/base/async/cancellation.md).

