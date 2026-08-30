---
title: NGIN CLI
description: The authoritative native interface for project authoring, resolution, builds, deployment, and tooling.
---

# NGIN CLI

The `ngin` executable turns authored manifests into resolved work. Run it
without arguments for the command list supported by the exact binary on your
machine.

## Daily loop

```bash
ngin validate
ngin build
ngin run
```

Use [the daily workflow](./cli-workflow.md) for a guided path and the
[command map](../reference/cli.md) for command families.

## Explicit selection

```text
--project <file.nginproj>
--workspace <file.ngin>
--configuration <name>
--target <name-or-alias>
--toolchain <name>
--run <name>
--profile <name>
--option <Name=Value>
```

A profile is a workspace feature. Explicit standalone project commands usually
select configuration and other build-context dimensions directly.

## Human and machine output

```bash
ngin graph
ngin inspect --format json
ngin schema --format json
```

Diagnostics go to stderr. Requested machine-readable payloads go to stdout, so
automation can process structured output without scraping diagnostic text.

## Exit behavior

Invalid syntax, failed resolution, stale locks, denied actions, backend or
staging failures, and failed child processes return nonzero. Semantic `diff`
uses exit code `2` when resolved graphs differ.
