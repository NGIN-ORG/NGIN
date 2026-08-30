---
title: Collect NGIN diagnostics
description: Capture a small, reproducible, non-secret report for NGIN manifest, graph, build, stage, and runtime failures.
---

# Collect NGIN diagnostics

A useful report lets another developer reproduce the same selection without
receiving secrets or an entire generated workspace.

## Record versions and platform

Capture the NGIN CLI output/help identification available in your build plus:

```bash
cmake --version
c++ --version
```

State the operating system, architecture, generator, compiler, and whether the
failure reproduces from a clean generated output directory.

## Capture authored and effective intent

Include the smallest relevant authored manifest fragment and commands:

```bash
ngin validate --project App.nginproj --profile Debug
ngin inspect --effective --format json \
  --project App.nginproj --profile Debug
ngin graph --format json --project App.nginproj --profile Debug
```

If the diagnostic names a graph identity:

```bash
ngin explain <identity> --format json \
  --project App.nginproj --profile Debug
```

Machine-readable payloads go to stdout and diagnostics to stderr. Capture both
without merging them when downstream tooling needs valid JSON.

## Capture the first failed operation

Provide the exact command, current working directory, exit code, and full
stderr from the first failing phase. For a compiler/linker failure, include the
first diagnostic and its command line. For runtime failure, include the staged
layout path and application/Core structured error.

## Redact safely

Remove tokens, passwords, private keys, personal paths when irrelevant,
environment secrets, proprietary source, and user data. Preserve structural
shape, portable error codes, native error numbers, package names/versions,
relative paths, graph identities, and dependency paths.

Do not post an unreviewed environment dump, config snapshot, lock credential,
crash dump, or log file.

## Minimal issue checklist

- Expected outcome and actual outcome.
- Exact command and exit code.
- Smallest manifest/project that reproduces it.
- CLI, OS, architecture, CMake, compiler, and generator versions.
- `validate`, effective inspection, and graph result.
- First relevant stderr diagnostic.
- Whether the issue reproduces after regenerating only the exact disposable
  output directory.

