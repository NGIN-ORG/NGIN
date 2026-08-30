---
title: NGIN tools
description: Use the native CLI, VS Code integration, generators, analyzers, and formatters built around one Composition Graph.
---

# NGIN tools

The CLI is the executable authority for project semantics. Editor and package
tools consume the same graph and versioned protocols instead of implementing
another resolver.

## Tool map

| Tool | Responsibility |
| --- | --- |
| [NGIN CLI](./tools/cli.md) | Validate, inspect, restore, configure, build, stage, run, test, benchmark, and publish |
| [VS Code extension](./tools/vscode.md) | Product-aware navigation, authoring, build context, tasks, tests, diagnostics, and debugging |
| [MetaGen](./tools/metagen.md) | Generate reflection registration from compiler-preprocessed headers |
| [Tooling packages](./tools/tooling-packages.md) | Select explicit analyzer and formatter actions through packages |

## One semantic authority

```text
                         ┌─► terminal commands
manifests ─► CLI/graph ──┼─► VS Code protocol
                         ├─► generators and actions
                         └─► build, stage, and launch plans
```

If a tool needs project membership, package activation, compile context, or
runtime intent, it should ask the CLI rather than infer those facts from the
filesystem.
