---
title: NGIN reference
description: Look up exact manifest shapes, command families, and machine-readable documentation endpoints.
---

# Reference

Reference pages describe exact contracts. Start with a guide when learning a
workflow; use these pages when you already know what you need to look up.

## Manifest reference

| Manifest | File pattern |
| --- | --- |
| [Project manifest](./reference/project-manifest.md) | `*.nginproj` |
| [Package manifest](./reference/package-manifest.md) | `*.nginpkg` |
| [Workspace manifest](./reference/workspace-manifest.md) | `*.ngin` |

## Tool reference

- [CLI command map](./reference/cli.md)
- [Documentation for AI](./reference/ai-access.md)

## C++ API reference

Each library has its own Doxygen reference, search, and indexes:

- [NGIN.Base](/reference/doxygen/base/index.html)
- [NGIN.Core](/reference/doxygen/core/index.html)
- [NGIN.Reflection](/reference/doxygen/reflection/index.html)
- [NGIN.ECS](/reference/doxygen/ecs/index.html)
- [NGIN.UI](/reference/doxygen/ui/index.html)
- [NGIN.Log](/reference/doxygen/log/index.html)
- [NGIN.UI.Hosting](/reference/doxygen/ui-hosting/index.html)
- [NGIN.UI.Backend.SDL3](/reference/doxygen/ui-sdl3/index.html)
- [NGIN.UI.Accessibility.Windows](/reference/doxygen/ui-accessibility-windows/index.html)

For usage examples, see the [API guides](./api/index.md).

## Executable authority

The running CLI is authoritative for its accepted commands and manifest
schema:

```bash
ngin
ngin schema --format json
```

When a static page and a newer executable disagree, use the executable and
report the documentation drift.
