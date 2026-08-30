---
title: How NGIN works
description: Understand products, manifests, resolution, generated builds, and staged runtime layouts.
---

# How NGIN works

NGIN separates what you author from what tools derive.

## Authored intent

You author three manifest types:

| Manifest | Describes |
| --- | --- |
| `.nginproj` | One physical executable or library product |
| `.nginpkg` | A reusable package, its exports, and build integration |
| `.ngin` | A workspace containing discovery, versions, profiles, and policy |

One project describes one product. The project root is either `Executable` or
`Library`; a library chooses `Static`, `Shared`, `Interface`, or `Plugin`.

## Semantic resolution

The CLI combines authored manifests, selected build context, package locks, and
provider information into one resolved Composition Graph.

```text
Project + Workspace + Packages + Build Context
                         │
                         ▼
                 Composition Graph
```

The graph is the source of truth. Build generation, staging, running, testing,
publishing, inspection, and editor integration consume the same resolved model.
They should not independently reinterpret the authored XML.

## Generated build backend

NGIN currently generates CMake and uses the platform compiler toolchain. The
generated files are inspectable output, not the normal authoring surface.

```text
.nginproj ──► ngin ──► generated CMake ──► Ninja/compiler
```

Do not edit generated files to change product behavior. Change the project,
package, or workspace manifest that supplied the intent.

## Staged applications

A successful compiler invocation is not always a runnable application. Native
libraries, assets, plugins, notices, configuration, and generated resources may
also be required. NGIN derives a StagePlan from the graph and creates an
explicit staged layout.

`ngin run` derives its RunPlan from the same graph and targets the staged
product. This keeps command-line runs, tests, publishing, and editor launch
behavior aligned.

## Build context

Configuration, target, toolchain, profile, options, and selected Run intent are
explicit inputs. A workspace profile is a named set of build-context choices;
it is not a second project grammar.

## A useful debugging order

When something is wrong:

1. Run `ngin validate`.
2. Inspect `ngin graph` or `ngin inspect --format json`.
3. Confirm the selected build context and package lock.
4. Inspect generated build files only after the resolved graph is correct.

That order follows the actual flow of authority.
