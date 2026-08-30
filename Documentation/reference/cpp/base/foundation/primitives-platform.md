---
title: Primitives and platform API
description: API reference for NGIN fixed-width aliases, size types, byte types, exports, and platform/compiler definitions.
---

# Primitives and platform API

## Primitive aliases

**Header:** `<NGIN/Primitives.hpp>`

NGIN exposes fixed-width signed/unsigned integer aliases, floating aliases,
size/count aliases such as `UIntSize`, and `Byte`. Use them where a public NGIN
contract needs explicit width or the shared vocabulary. They remain normal C++
arithmetic types and retain normal promotion, overflow, and conversion rules.

Do not serialize native object representation merely because fields use fixed-
width aliases. A format must still define byte order, layout, versioning, and
validation.

## Defines and visibility

**Header:** `<NGIN/Defines.hpp>`

This header centralizes supported platform/compiler detection, import/export
visibility, calling/attribute helpers, and component API declarations. Public
code should use the documented component export macro rather than reproducing
compiler-specific attributes.

Conditional compilation should test the public NGIN platform/config surface.
Do not infer runtime CPU or provider capabilities solely from compiler macros;
build capability and runtime availability are separate questions.

[Browse `Primitives.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Primitives.hpp) and [`Defines.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Defines.hpp).
