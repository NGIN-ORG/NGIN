---
title: Owning callables
description: Store invocation state with Callable while keeping signatures, captures, allocation, and empty state explicit.
---

# Owning callables with `Callable`

`Callable<R(Args...)>` is an owning type-erased callable with a fixed function
signature and small-buffer storage.

```cpp
NGIN::Utilities::Callable<int(int)> doubleValue =
    [](int value) { return value * 2; };

int result = doubleValue(21);
```

The signature is part of the type, so argument and return conversion happens at
the normal C++ call boundary. Empty-state inspection and invocation behavior are
defined by the declaration; do not invoke a moved-from or reset callable without
checking it.

Small callable objects fit in the inline buffer; larger or unsuitable objects
use heap storage. Copying copies captured state; moving transfers/moves it.
Allocation and user constructors can throw even though later destruction is
non-throwing.

An owning wrapper does not make referenced captures safe:

```cpp
int local = 1;
Callable<void()> unsafe = [&] { Use(local); };
```

`unsafe` may outlive `local`. Capture values or shared ownership when invocation
can escape the current scope. Be especially explicit when placing a callable in
an executor, async operation, or long-lived registry.

Prefer a template parameter for hot, single-caller generic code where type
erasure is unnecessary. Use `Callable` when one stable stored signature is the
useful abstraction.
