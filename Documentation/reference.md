---
title: NGIN reference
description: Look up exact manifest shapes, command families, and machine-readable documentation endpoints.
---

# Reference

Reference pages describe exact contracts. Start with a guide when learning a
workflow; use these pages when you already know what you need to look up.

## Manifest reference

| Contract | Reference |
| --- | --- |
| One executable or library product | [Project manifest](./reference/project-manifest.md) |
| Reusable exports and integration | [Package manifest](./reference/package-manifest.md) |
| Discovery, profiles, versions, and policy | [Workspace manifest](./reference/workspace-manifest.md) |

## Tool reference

- [CLI command map](./reference/cli.md)
- [Documentation for AI](./reference/ai-access.md)

## C++ API reference

- [All C++ libraries and symbols](./reference/cpp/index.md)
- [NGIN.Base](./reference/cpp/base/index.md), including the detailed
  [Async symbol reference](./reference/cpp/base/async/index.md)
- [NGIN.Core](./reference/cpp/core/index.md),
  [Reflection](./reference/cpp/reflection/index.md),
  [ECS](./reference/cpp/ecs/index.md), [UI](./reference/cpp/ui/index.md), and
  [Log](./reference/cpp/log/index.md)
- [Cross-cutting API guides](./api/index.md)

## Executable authority

The running CLI is authoritative for its accepted commands and manifest
schema:

```bash
ngin
ngin schema --format json
```

When a static page and a newer executable disagree, use the executable and
report the documentation drift.
