---
title: Learn errors and typed results
description: Model values, absence, recoverable failures, exceptions, and error translation explicitly.
---

# Learn errors and typed results

NGIN.Base distinguishes a returned value, reasonless absence, recoverable
failure, cancellation, and a thrown exception. Keeping those states distinct
makes the caller's responsibilities visible.

## Start here

1. [Choose a failure shape](./results/choosing-a-shape.md).
2. Learn [`Expected` and `Optional`](./results/expected-optional.md).
3. Define [error translation and exception boundaries](./results/error-boundaries.md).
4. Look up exact declarations in the [Results API](../../reference/cpp/base/results.md).

## Smallest example

```cpp
#include <NGIN/Utilities/Expected.hpp>

enum class ParseNumberError { Empty, Invalid };

NGIN::Utilities::Expected<int, ParseNumberError>
ParseNumber(std::string_view text) {
    if (text.empty())
        return NGIN::Utilities::Unexpected{ParseNumberError::Empty};
    // Parse and return either the value or a typed error.
    return 42;
}
```

`Expected<T, E>` and `Unexpected<E>` are aliases of the C++ standard expected
types. Normal expected operations therefore apply. `Optional<T>` is the
standard optional alias.

## Rules of thumb

- Use `Expected<T, E>` when the caller can report, retry, translate, or choose
  another path based on why an operation failed.
- Use `Optional<T>` when absence is the complete answer.
- Follow an API's documented exception contract. Do not wrap every exception
  in a vague error simply to make the signature look uniform.
- At ABI, thread, coroutine-owner, callback, or `noexcept` boundaries, translate
  deliberately and retain operation/domain/native context.
- Cancellation is a control-flow outcome in NGIN.Async, not an arbitrary domain
  error string.

An expected-returning function is not automatically `noexcept`: allocation and
user callbacks may still throw. Check the declaration you actually call.
