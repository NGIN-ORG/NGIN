---
title: Build, stage, and run
description: Follow a product from generated build metadata to a runnable staged layout.
---

# Build, stage, and run

NGIN treats compiling and producing a runnable application as related but
distinct operations.

## Configure and build

```bash
ngin configure --configuration Debug
ngin build --configuration Debug
```

Configure generates backend metadata. Build invokes the generated backend and
native toolchain for the selected product.

## Stage runtime inputs

```bash
ngin stage --configuration Debug
```

The StagePlan collects the product artifact and its required runtime files,
assets, shared libraries, plugins, configuration, and notices into an explicit
layout. Stage intent comes from the resolved graph rather than post-build copy
commands hidden in handwritten CMake.

## Run the staged product

```bash
ngin run --configuration Debug
```

Run derives a RunPlan from the selected product and named Run intent. The plan
identifies the executable, working directory, arguments, environment, and
staged dependencies.

## Test and benchmark are separate intents

```bash
ngin test --configuration Debug
ngin benchmark --configuration Release
```

Tests and benchmarks have distinct registrations and plans. They are not
inferred from arbitrary executable names.

## Output is generated

Normal workspace output is placed under `.ngin/build/`. Explicit commands may
select another output directory. Generated build trees, staged layouts, and
launch files are disposable products of authored manifests and should not be
edited.
