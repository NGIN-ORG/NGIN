---
title: C++ API reference
description: Code-grounded reference for NGIN public headers, namespaces, types, functions, ownership, and failures.
---

# C++ API reference

This section is for looking up the exact C++ surface. It is organized like a
traditional generated reference: **library → subsystem → symbol**. Every symbol
page identifies its header, namespace, link target, declaration, members,
ownership, errors, and source location.

If you are learning a feature, begin in [Libraries](../../libraries.md). Those
pages teach the model and provide complete examples. Come here once you need to
confirm a signature or contract.

## Libraries

| Library | Namespace | Main include | Reference |
| --- | --- | --- | --- |
| NGIN.Base | `NGIN`, `NGIN::Async`, and subsystem namespaces | `<NGIN/NGIN.hpp>` | [Open Base](./base/index.md) |
| NGIN.Core | `NGIN::Core` | `<NGIN/Core/Core.hpp>` | [Open Core](./core/index.md) |
| NGIN.Reflection | `NGIN::Reflection` | `<NGIN/Reflection/Reflection.hpp>` | [Open Reflection](./reflection/index.md) |
| NGIN.ECS | `NGIN::ECS` | `<NGIN/ECS/ECS.hpp>` | [Open ECS](./ecs/index.md) |
| NGIN.UI | `NGIN::UI` | `<NGIN/UI/UI.hpp>` | [Open UI](./ui/index.md) |
| NGIN.Log | `NGIN::Log` | `<NGIN/Log/Log.hpp>` | [Open Log](./log/index.md) |

## What belongs here

- public classes, structs, enums, concepts, aliases, and free functions;
- exact includes and CMake targets;
- parameter, return-value, and error contracts;
- ownership, invalidation, allocation, and thread-safety rules;
- links to declarations in the checked-in source.

Tutorial explanation stays in Libraries. Design and usage discussion that
spans several symbols stays in the [API guides](../../api/index.md). Names in a
lowercase `detail` namespace or directory are not public contracts.

## Version

This reference describes the repository revision from which the site was
built. NGIN is experimental and pre-1.0 APIs can change. The linked declaration
is authoritative when a page and the checked-out code disagree.

