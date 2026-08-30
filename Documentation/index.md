---
layout: home
title: NGIN Documentation
description: One model for building, composing, and shipping modern C++ applications.

hero:
  name: NGIN
  text: One model for modern C++ applications.
  tagline: Build, compose, and ship native products through one project system—then adopt NGIN libraries wherever your application needs them.
  actions:
    - theme: brand
      text: Build your first project
      link: /start/first-project
    - theme: alt
      text: Explore libraries
      link: /libraries

features:
  - icon: "01"
    title: The project system
    details: Describe one product, resolve one Composition Graph, and derive build, stage, run, test, and tooling behavior from it.
    link: /project-system
    linkText: Explore the project system
  - icon: "02"
    title: Modular C++ libraries
    details: Enter NGIN.Base, Core, Reflection, ECS, UI, or Log, then learn only the subsystem your application needs.
    link: /libraries
    linkText: Choose a library
  - icon: "03"
    title: Exact contracts
    details: Look up public headers, symbols, CLI commands, manifests, ownership, errors, and source declarations.
    link: /reference
    linkText: Open reference
---

## One platform, two paths

The project system and the libraries work together, but neither forces you to
adopt the other.

| Path | Use it when you want to… | Begin |
| --- | --- | --- |
| **Project system** | Define a C++ product, consume packages, share workspace policy, build, stage, run, test, or publish it | [Start building](./start.md) |
| **Libraries** | Add Async, Networking, Memory, Core hosting, Reflection, ECS, UI, Log, or another focused C++ capability | [Explore libraries](./libraries.md) |

```text
NGIN
├── Project system
│   ├── Projects
│   ├── Packages
│   ├── Workspaces
│   └── Composition Graph and CLI
└── Libraries
    ├── NGIN.Base ── Async, Networking, Memory, Serialization, ...
    ├── NGIN.Core
    ├── NGIN.Reflection
    ├── NGIN.ECS
    ├── NGIN.UI
    └── NGIN.Log
```

## From intent to a running product

NGIN keeps the native toolchain visible. Authored manifests express product
intent; the Composition Graph becomes the shared source of truth for every
operation that follows.

```text
authored manifests
       │
       ▼
Composition Graph ──► generated CMake ──► native compiler
       │
       ├────────────► staged application
       ├────────────► run and test plans
       └────────────► editor and inspection data
```

## Already know what you need?

- [Look up a C++ type, function, or header](./reference/cpp/index.md).
- [Find a CLI command or manifest contract](./reference.md).
- [Diagnose a build, stage, run, or package failure](./troubleshooting/index.md).
- [Use raw Markdown and AI discovery endpoints](./reference/ai-access.md).

> [!WARNING]
> NGIN is experimental. Manifests, package contracts, library APIs, and native
> plugin ABI may change before the first stable release.
