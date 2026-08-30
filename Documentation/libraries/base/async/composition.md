---
title: Combine async tasks
description: Await tasks sequentially, run them together, race alternatives, and understand failure and lifetime behavior.
---

# Combine async tasks

Use direct `co_await` for dependent steps, `WhenAll` for independent work whose
results are all required, and `WhenAny` for alternatives where the first
terminal child wins.

## Sequential dependency

```cpp
auto user = co_await LoadUser(context, id);
auto permissions = co_await LoadPermissions(context, user);
co_return BuildSession(user, permissions);
```

This is correct when the second operation needs the first result. Any
non-success propagates out of the parent.

## Wait for all

For value tasks with the same domain-error type, `WhenAll` returns a tuple:

```cpp
auto [user, settings] = co_await NGIN::Async::WhenAll(
    context,
    LoadUser(context, id),
    LoadSettings(context, id));
```

For void tasks, success means every task succeeded. The tasks are consumed;
move named tasks:

```cpp
auto first = WarmCache(context);
auto second = OpenIndex(context);
co_await NGIN::Async::WhenAll(
    context, std::move(first), std::move(second));
```

Do not read the moved task afterward.

## Race alternatives

`WhenAny` takes factories rather than already-created tasks:

```cpp
auto index = co_await NGIN::Async::WhenAny(
    context,
    [](NGIN::Async::TaskContext& child) {
        return QueryPrimary(child);
    },
    [](NGIN::Async::TaskContext& child) {
        return QueryReplica(child);
    });
```

Factories matter because the combinator creates a separate child context for
each candidate. Those contexts link parent cancellation and let the winner
cancel losers without canceling unrelated parent work.

## What “first” means

The winner is the first child to reach any terminal state: success, domain
error, canceled, or fault. `WhenAny` does not wait for the first *success* while
silently ignoring earlier failures.

After selecting the winner it:

1. records the winner and its terminal outcome;
2. requests cancellation through every losing child context;
3. waits for all loser watchers to finish;
4. returns the winning index on success or propagates the winning non-success.

The drain step makes parent-frame captures safe. It also exposes tasks that do
not cooperate with cancellation.

## Captures and factories

The factory is invoked while the parent frame is alive:

```cpp
std::string request = BuildRequest();

auto winner = co_await NGIN::Async::WhenAny(
    context,
    [&](auto& child) { return Send(child, primary, request); },
    [&](auto& child) { return Send(child, replica, request); });
```

Borrowing `request` is safe only because the combinator drains both tasks
before returning. The async operation called by `Send` must still obey its own
buffer-lifetime contract.

## Choose the right shape

| Need | Use |
| --- | --- |
| Step B requires A | Direct sequential `co_await` |
| A and B are independent and both required | `WhenAll` |
| First terminal alternative decides | `WhenAny` |
| Work should outlive this scope intentionally | An explicit owning service/root operation, rarely `Detach` |

Do not use `WhenAny` as a timeout by adding a timer unless loser cancellation
and drain latency match your timeout policy. A source with `CancelAfter` can be
clearer for one operation.

Next: [contexts and schedulers](./runtime.md), or inspect
[`WhenAll` and `WhenAny`](../../../reference/cpp/base/async/combinators.md).

