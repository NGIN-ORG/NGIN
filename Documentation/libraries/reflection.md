---
title: NGIN.Reflection
description: Explicit C++23 runtime reflection for fields, properties, methods, constructors, enums, attributes, and modules.
---

# NGIN.Reflection

`NGIN.Reflection` provides one runtime descriptor model for local and imported
modules. Types opt in explicitly through `TypeBuilder<T>` or generated builder
code.

## Start here

1. Register a type in the [quick start](./reflection/quick-start.md).
2. Learn the [registration model](./reflection/registration.md).
3. Use [MetaGen](./reflection/metagen.md) when annotations should generate the
   same registration calls.
4. Read [modules and lifetimes](./reflection/modules-lifetimes.md) before
   importing or unloading reflection modules.
5. Use the [Reflection C++ reference](../reference/cpp/reflection/index.md) for descriptors,
   values, instances, member access, invocation, adapters, and errors.

## Reflected surface

The descriptor model covers:

- types, fields, and properties;
- methods, constructors, and free functions;
- enums and base relationships;
- attributes;
- module import and unload.

Reflection is not required for `NGIN.Core` typed constructor injection. Adopt
it when the application genuinely needs runtime metadata, inspection,
serialization mapping, tooling, or data-driven construction.

The shortest API path is `ModuleRegistration` → `RegisterType<T>` → `Commit`
→ `GetType` → checked descriptor/member operations. Generated MetaGen output
uses that same registration model.

Use the [symbol index](../reference/cpp/reflection/index.md) for code lookup and
the [Reflection API guide](../api/reflection.md) for end-to-end contracts.

> [!WARNING]
> Binary compatibility is not promised before 1.0. Imported native modules must
> be trusted and ABI-compatible.
