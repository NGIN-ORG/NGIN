---
title: Expected and Optional
description: Construct, inspect, transform, and propagate NGIN result aliases using standard C++ semantics.
---

# `Expected` and `Optional`

```cpp
#include <NGIN/Utilities/Expected.hpp>
#include <NGIN/Utilities/Optional.hpp>

using Result = NGIN::Utilities::Expected<Record, LoadError>;
using MaybeRecord = NGIN::Utilities::Optional<Record>;
```

These are direct aliases for `std::expected` and `std::optional`. Use their
standard constructors, observers, monadic operations supported by the active
C++ standard library, and `std::nullopt`/`std::unexpected` conventions.

## Propagate without destroying context

```cpp
auto loaded = LoadRecord(id);
if (!loaded)
    return NGIN::Utilities::Unexpected{loaded.error()};

Record record = std::move(loaded).value();
```

Check before calling `value()` unless throwing `bad_expected_access` is truly
the desired contract. When moving an expensive result, move the expected or
its contained value intentionally.

`Expected<void, E>` represents success with no value. It is preferable to a
boolean when failure has useful information.

## Optional is not validation

`value_or` supplies a default for ordinary absence. It should not be used to
silently accept malformed configuration or security-sensitive input. If the
default changes behavior materially, make the policy visible at the call site.

## Exceptions still exist

An expected-returning operation can allocate, invoke callbacks, or manipulate
types whose constructors throw. The alias itself does not add `noexcept`.
Inspect the exact declaration and constrain callback contracts where a
non-throwing boundary is required.
