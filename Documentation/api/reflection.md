---
title: NGIN.Reflection API
description: Registration, descriptors, values, instances, fields, properties, methods, constructors, modules, adapters, and MetaGen integration.
---

# NGIN.Reflection API

**Include:** `<NGIN/Reflection/Reflection.hpp>`  
**Package:** `NGIN.Reflection`  
**Namespace:** `NGIN::Reflection`

Reflection is opt-in. A type is visible only after authored or generated
builder code commits it into a module.

## Register a type

```cpp
struct User {
    int id {};

    friend void NginReflect(
        NGIN::Reflection::Tag<User>,
        NGIN::Reflection::TypeBuilder<User>& builder) {
        builder.SetName("App::User");
        builder.Field<&User::id>("id");
    }
};

NGIN::Reflection::ModuleRegistration module {"App"};
module.RegisterType<User>();
if (!module.Commit()) return 1;
```

`TypeBuilder<T>` can register fields, properties, methods, constructors, enum
values, bases, and attributes. `ModuleRegistration::RegisterTypes<T...>()`
registers several generated/authored descriptors and `RegisterFunction`
registers a free function.

## Lookup and descriptors

```cpp
auto type = NGIN::Reflection::GetType("App::User");
if (!type) return;

std::string_view name = type->QualifiedName();
std::size_t fields = type->FieldCount();
auto id = type->GetField("id");
```

| Descriptor | Main operations |
| --- | --- |
| `Type` | identity/name/kind, construct, enumerate/find members, bases, enum values, attributes |
| `Field` | `Name`, `TypeName`, `Read`, `Write`, attributes |
| `Property` | `Name`, `TypeName`, readable/writable checks, `Read`, `Write`, attributes |
| `Method` | name, parameter/return metadata, `Invoke`, attributes |
| `Constructor` | parameter metadata, injectable bindings, `Invoke`, attributes |
| `Base` | name, checked upcast/downcast |
| `Function` | name, signature metadata, invocation, attributes |
| `Module` | identity, types/functions, import state and lifetime |

Descriptor handles can be invalid. Call `IsValid()` where a value did not come
from a successful expected-like lookup.

## Values and instances

`Value` is the reflection transport value. Use `TryAs<T>()` to inspect a
compatible stored value rather than assuming the reflected declaration's text
name is sufficient for a C++ cast.

`InstanceRef` represents reflected object storage with module-aware lifetime
and mutability. `Construct`/constructor invocation returns an owned instance.
Fields, properties, and methods validate the instance and return expected-like
errors.

```cpp
auto instance = type->Construct();
auto field = type->GetField("id");
if (!instance || !field) return;

NGIN::Reflection::Value newId {std::int64_t {7}};
auto wrote = field->Write(*instance, newId);
auto read = field->Read(*instance);
```

Module ownership is retained by reflected values and instances. Unload is
rejected while live reflected instances still depend on that module.

## Invocation errors

Construction, conversion, reads, writes, and invocation return typed
expected-like results with `Error`. Typical causes are invalid handles,
missing members, wrong argument count/type, non-readable/non-writable members,
failed conversions, thrown user code translated at the ABI boundary, and
module lifetime conflicts.

Check the error before accessing the value. Reflection errors are useful
developer diagnostics; do not use reflection as input validation by itself.

## Constructor injection metadata

Builder parameter bindings support required, optional, named, and named
optional dependencies through `ConstructorDependency<T>`,
`OptionalConstructorDependency<T>`, and their named forms. NGIN.Core can read
this metadata when reflection-driven construction is enabled.

## Collection adapters

`Adapters.hpp` supplies adapters for sequence, tuple-like, variant-like,
optional, standard map, and NGIN flat-map shapes. Adapters borrow the wrapped
object unless their construction explicitly transfers ownership. Mutating an
underlying collection can invalidate element views according to that
collection's rules.

## MetaGen

MetaGen emits the same builder model from annotated headers. In a product
manifest:

```xml
<Generate Using="NGIN.Reflection.MetaGen/ReflectionCodegen" Version="0.1">
  <Header Include="src/**/*.hpp" />
</Generate>
```

Generated registration code is build output; edit the annotated header or
generator configuration, not the generated file. The
[`Hello.Reflection`](https://github.com/NGIN-ORG/NGIN/tree/main/Examples/Hello.Reflection)
example exercises generation, lookup, construction, fields, properties,
methods, constructors, and enums.

## Module ABI

Imported modules expose `NGINReflectionModuleApi` through
`NGINReflectionGetModuleApi`. This is trusted native code. Before 1.0, binary
compatibility is not promised; build both sides for a compatible compiler,
runtime, architecture, and NGIN Reflection ABI.

**Source:** [`NGIN.Reflection` public headers](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Reflection/include/NGIN/Reflection)

