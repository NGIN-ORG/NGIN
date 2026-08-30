---
title: Traits and compile-time inspection
description: Apply function, enum, and type traits as public constraints rather than leaking template machinery.
---

# Traits and compile-time inspection

NGIN.Meta provides `FunctionTraits`, `EnumTraits`, and shared type traits used
by generic NGIN facilities. Traits expose compile-time shape; they do not
perform runtime reflection.

Use a named concept or a small `requires` expression at the public boundary:

```cpp
template<class F>
concept NullaryCallable = requires(F&& function) {
    std::forward<F>(function)();
};
```

When an NGIN trait already expresses the exact requirement, use it rather than
reconstructing implementation-specific detection. Keep the constraint close to
the invalid expression so compiler diagnostics point to the user's call.

`FunctionTraits` is useful where a framework must describe a supported callable
signature. Not every overloaded/generic call operator has one unambiguous
signature; prefer an explicit signature parameter when inference would be
unclear.

`EnumTraits` supports enum metadata conventions. A specialization is a compile-
time description, not proof that arbitrary integers form valid enum values.

Avoid exposing detail traits in public return types or error messages. Test both
accepted and rejected shapes for foundational concepts: a trait that is only
tested on valid inputs often produces poor behavior on the boundary where it
matters.
