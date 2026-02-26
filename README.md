# NGIN

NGIN is the umbrella workspace and release train for the NGIN application engine platform.

This repository coordinates independently versioned component repositories such as:

- `NGIN.Base`
- `NGIN.Core`
- `NGIN.Reflection`
- `NGIN.ECS`
- future platform/runtime/editor/build repos

## What This Repo Is

- Platform architecture/specification source of truth
- Compatibility manifest (`manifests/platform-release.json`)
- Integration workspace bootstrap (CMake + tooling)
- Home for future cross-repo platform code (runtime kernel, build orchestration, editor host)

## What This Repo Is Not

- A monorepo replacement for all `NGIN.*` repos (today)
- A single library target consumed directly by applications

## Repository Model

NGIN uses a **federated/polyrepo** model:

- component repos stay first-class and independently consumable
- this repo publishes a **single platform version** that pins a compatible set of component refs

## Quick Start (Workspace)

List pinned components:

```bash
python tools/ngin-sync.py list
```

Prepare or sync local checkouts into `workspace/externals`:

```bash
python tools/ngin-sync.py sync
```

Configure the workspace metadata project:

```bash
cmake --preset dev
```

## Docs

- Architecture: `docs/architecture/NGIN-Architecture.md`
- First detailed spec: `docs/specs/001-module-dependency-graph.md`
