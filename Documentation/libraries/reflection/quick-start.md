---
title: NGIN.Reflection quick start
description: Opt a C++ type into reflection and query its committed descriptor.
---

# NGIN.Reflection quick start

## Before you start

You need the NGIN CLI, a C++23 compiler, and a workspace that discovers
`NGIN.Reflection`. Create `ReflectionDemo/ReflectionDemo.nginproj` and
`ReflectionDemo/src/main.cpp`.

## Add the package

```xml
<Executable Name="ReflectionDemo">
  <Uses>
    <Package Name="NGIN.Reflection" Version="0.1" />
  </Uses>
  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
</Executable>
```

## Register a type

```cpp
#include <NGIN/Reflection/Reflection.hpp>

struct User {
    int id{};

    friend void NginReflect(NGIN::Reflection::Tag<User>,
                            NGIN::Reflection::TypeBuilder<User>& builder) {
        builder.SetName("User");
        builder.Field<&User::id>("id");
    }
};

int main() {
    NGIN::Reflection::ModuleRegistration module{"App"};
    module.RegisterType<User>();
    if (!module.Commit()) {
        return 2;
    }
    return NGIN::Reflection::GetType("User") ? 0 : 1;
}
```

Registration is explicit. Including a header does not silently scan or mutate
a global registry.

## Build and run

```bash
ngin validate --project ReflectionDemo/ReflectionDemo.nginproj --configuration Debug
ngin build --project ReflectionDemo/ReflectionDemo.nginproj --configuration Debug
ngin run --project ReflectionDemo/ReflectionDemo.nginproj --configuration Debug
```

Exit `0` confirms registration committed and lookup returned a descriptor.
Exit `2` means commit failed; exit `1` means the committed name was not found.

## If it fails

- Registration is not automatic: `RegisterType<T>()` and `Commit()` must run.
- Lookup uses the name authored with `SetName`, including any namespace.
- For generated registration, inspect the Generate action in the graph and fix
  the annotated header or MetaGen configuration, not generated output.

Continue with the [registration model](./registration.md), or use
[MetaGen](./metagen.md) to generate registrations from annotated headers.
The [Reflection C++ reference](../../reference/cpp/reflection/index.md) covers descriptors,
values, member access, invocation, errors, and module lifetimes.
