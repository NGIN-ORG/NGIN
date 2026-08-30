---
title: Composition Graph
description: Inspect the resolved source of truth shared by builds, runtime plans, publishing, and tooling.
---

# Composition Graph

The Composition Graph is NGIN's resolved semantic model. It is produced from
authored manifests and the selected build context, then consumed by every
downstream operation.

## Why it exists

Without a shared graph, a build command, an editor, a publisher, and a runtime
launcher can each interpret the project differently. NGIN resolves once and
derives each operation from that result.

```text
                         ┌─► generated build
authored inputs ─► graph ├─► stage/run/test plans
                         ├─► package and publish plans
                         └─► editor and inspection protocol
```

## Human and machine inspection

```bash
ngin graph
ngin inspect --format json
```

Use `graph` for an interactive explanation and `inspect --format json` for
automation. Machine consumers should check the protocol version rather than
assuming fields from an unrelated CLI revision.

## Provenance

Resolved values retain where they came from. This lets explanation and editor
tooling distinguish authored values, workspace defaults, profile choices,
package exports, and derived artifacts.

When an include directory, link, runtime asset, or tool appears unexpectedly,
use graph explanation to find its provenance instead of searching generated
CMake first.
