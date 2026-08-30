---
title: Workspaces
description: Coordinate products, package discovery, versions, profiles, capabilities, and policy.
---

# Workspaces

A project can build without a workspace. Add a workspace when several products
need shared discovery, version selection, profiles, capability preferences, or
trust policy.

## Workspace responsibilities

A workspace may provide:

- product and CMake-project discovery;
- package sources and providers;
- shared versions and options;
- named profiles;
- platform and capability preferences;
- dependency trust and reproducibility policy.

It does not wrap project grammar or redefine a product. Each `.nginproj`
remains the authority for its own executable or library.

## Profiles

A profile supplies named build-context choices. Select one with:

```bash
ngin build --workspace NGIN.ngin --profile Debug
```

Profiles are workspace features, so `--profile` requires a workspace. For a
standalone project, pass explicit context such as `--configuration Debug`.

## Inspect selection

Use the graph to confirm which workspace, project, profile, package instances,
and provider choices were resolved:

```bash
ngin graph --workspace NGIN.ngin --profile Debug
ngin inspect --workspace NGIN.ngin --profile Debug --format json
```
