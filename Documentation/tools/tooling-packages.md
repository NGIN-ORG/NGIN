---
title: Analyzers and formatters
description: Select package-provided analysis and formatting actions explicitly and execute them under workspace trust policy.
---

# Analyzers and formatters

NGIN models analyzers and formatters as typed package actions backed by host
tools. A package being present does not authorize or execute its actions.

## Select tooling

```xml
<Tooling>
  <Analyze Using="NGIN.Tooling.ClangTidy/Analyze" />
  <Format Using="NGIN.Tooling.ClangFormat/Format" />
</Tooling>
```

Selecting an action introduces its package, typed export, and backing Tool in
host context.

## Run actions

```bash
ngin analyze --file src/main.cpp --format json
ngin tooling format --file src/main.cpp
```

Only actions of the requested kind execute. Machine-readable analyzer output is
appropriate for editor diagnostics and automated quality gates.

## Trust

Package actions execute native tools and therefore cross a trust boundary.
Workspace policy must allow the provider and action kind. Locks and integrity
checks must not be bypassed by editor integrations.

The runnable `Examples/Hello.Analyzer` and `Examples/Hello.Formatter` products
demonstrate the two canonical flows.
