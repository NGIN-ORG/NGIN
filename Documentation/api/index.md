---
title: C++ API guides
description: Cross-cutting explanations that connect related NGIN C++ symbols and contracts.
---

# C++ API guides

These pages explain how groups of related APIs fit together: choosing a
scheduler, selecting an allocator, handling I/O results, or understanding UI
backend contracts.

For exact types, signatures, headers, ownership, and source links, use the
[C++ API reference](/reference.md#c-api-reference). For a first successful program,
start in [Libraries](../libraries.md).

## C++ libraries

| Library | Main include | Guide | Symbol reference |
| --- | --- | --- | --- |
| NGIN.Base | `<NGIN/NGIN.hpp>` or a focused umbrella | [Base guides](./base/index.md) | [Base symbols](/reference/doxygen/base/namespaceNGIN.html) |
| NGIN.Core | `<NGIN/Core/Core.hpp>` | [Core guide](./core.md) | [Core symbols](/reference/doxygen/core/namespaceNGIN_1_1Core.html) |
| NGIN.Reflection | `<NGIN/Reflection/Reflection.hpp>` | [Reflection guide](./reflection.md) | [Reflection symbols](/reference/doxygen/reflection/namespaceNGIN_1_1Reflection.html) |
| NGIN.ECS | `<NGIN/ECS/ECS.hpp>` | [ECS guide](./ecs.md) | [ECS symbols](/reference/doxygen/ecs/namespaceNGIN_1_1ECS.html) |
| NGIN.UI | `<NGIN/UI/UI.hpp>` | [UI guide](./ui.md) | [UI symbols](/reference/doxygen/ui/namespaceNGIN_1_1UI.html) |
| NGIN.Log | `<NGIN/Log/Log.hpp>` | [Log guide](./log.md) | [Log symbols](/reference/doxygen/log/namespaceNGIN_1_1Log.html) |

The reference documents public contracts. Names under a lowercase `detail`
namespace or directory are implementation details even when an installed
public header includes them.

## Developer interfaces

- [CLI command reference](../reference/cli.md)
- [Project manifest reference](../reference/project-manifest.md)
- [Package manifest reference](../reference/package-manifest.md)
- [Workspace manifest reference](../reference/workspace-manifest.md)

## How these differ from the reference

Guides compare types and explain how to use them together. Doxygen generates
the reference directly from declarations and documentation comments in public
headers.

The API changes with the experimental platform. Use each page's **Source**
links when you need the exact declaration for the checked-out revision.
