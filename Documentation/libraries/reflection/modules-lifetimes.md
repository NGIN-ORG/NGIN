---
title: Reflection modules and lifetimes
description: Import native reflection modules without allowing descriptors or instances to outlive their owner.
---

# Modules and lifetimes

Imported modules expose a plain C ABI through `NGINReflectionModuleApi` and
`NGINReflectionGetModuleApi`. The C boundary locates the module API; the
reflection model above it remains typed C++.

## Ownership rule

Reflection values and instances retain their owning module. Unloading is
rejected while reflected instances or other owned values remain alive.

```text
module
 ├── descriptors
 ├── functions and constructors
 └── reflected instances

unload requires every escaping owner to be released
```

## Safe unload sequence

1. Stop creating values from the module.
2. Remove callbacks and registrations that can reach module code.
3. Release reflected instances and retained descriptors.
4. Request module unload and handle rejection.
5. Only then unload the native library.

## ABI boundary

Before 1.0, do not assume binary compatibility across compilers, standard
libraries, build modes, or unrelated NGIN revisions. Treat reflection modules
as trusted native artifacts produced for the exact application environment.
