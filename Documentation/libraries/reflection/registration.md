---
title: Reflection registration
description: Build and commit runtime descriptors through an explicit module registration transaction.
---

# Registration model

Reflection registration turns compile-time C++ knowledge into runtime
descriptors.

```text
C++ type ─► TypeBuilder<T> ─► ModuleRegistration ──commit──► registry
```

## Type builders

A builder declares the reflected name and selected members. Only registered
members become part of the descriptor; ordinary C++ members remain untouched.

The model can describe fields, properties, methods, constructors, enums, base
relationships, functions, and attributes.

## Module transaction

Group related registrations in `ModuleRegistration`. Commit publishes the
completed module to the runtime registry. Keep construction and publication
separate so partially constructed metadata is not observable.

## Names and identity

Choose stable qualified names at public boundaries. A C++ spelling, display
name, and cross-module identity do not always serve the same purpose. Avoid
deriving persistent identifiers from unstable compiler-specific type strings.

## Errors

Duplicate names, incompatible member declarations, missing dependencies, and
invalid base relationships should fail registration with enough context to
identify the module, type, and member involved.
