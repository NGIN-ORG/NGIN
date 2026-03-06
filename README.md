# NGIN

NGIN is the **umbrella workspace** for the NGIN platform.

NGIN is a **general C++ application platform** for building:

- games
- editors
- GUI applications
- CLI tools
- services
- domain-specific engines

This repo is mainly for:

- platform architecture and specs
- compatibility and release manifests
- workspace tooling for syncing and validating component repos

It is **not** the single application SDK library itself and does not produce a single `NGIN` library.

## What You Use This Repo For

If you are building an application:

- use component repos and packages directly such as `NGIN.Base`, `NGIN.Log`, `NGIN.Reflection`, and the current hosting implementation in `NGIN.Core`
- treat this repo as the platform definition and coordination layer

If you are shaping the platform:

- update specs and architecture docs
- update platform release and metadata manifests
- run workspace validation and project-based target resolution

If you are developing multiple NGIN components together:

- use this workspace to keep repos aligned and verify cross-repo compatibility

## Current Naming Note

The approved platform direction is to treat the central host/orchestration library as `NGIN.Core`.

Today, the implementation still lives in `NGIN.Core`.

## Quick Start

Use `python3` (or `py` on Windows).

1. Show platform-pinned components:

```bash
python3 tools/ngin.py list
```

2. Check local workspace health:

```bash
python3 tools/ngin.py doctor
python3 tools/ngin.py status
```

3. Sync missing pinned repos into `workspace/externals`:

```bash
python3 tools/ngin.py sync
```

4. Configure workspace helper project:

```bash
cmake --preset dev
```

5. Validate and resolve a target:

```bash
python3 tools/ngin.py package restore --project manifests/workspace.project.json
python3 tools/ngin.py validate --project manifests/workspace.project.json --locked --target NGIN.CoreSample
python3 tools/ngin.py graph --project manifests/workspace.project.json --locked --target NGIN.CoreSample
python3 tools/ngin.py resolve --project manifests/workspace.project.json --locked --target NGIN.CoreSample
```

6. Write the standard JSON report artifact set:

```bash
cmake --build build/dev --target ngin.reports
```

## Repository Map

- `docs/architecture/`: umbrella architecture and direction docs
- `docs/api-drafts/`: concrete public draft artifacts for upcoming platform APIs
- `docs/examples/`: example code and manifest shapes for draft platform workflows
- `docs/specs/`: normative platform specs
- `manifests/`: platform compatibility plus package/module/plugin metadata, schemas, and the canonical workspace project
- `tools/`: workspace tooling such as `ngin.py`
- `cmake/`: workspace CMake integration helpers

## Required Platform Components (Current)

- `NGIN.Base`
- `NGIN.Log`
- `NGIN.Core`
- `NGIN.Reflection`

## Read Next

- [NGIN Architecture](/home/berggrenmille/NGIN/docs/architecture/NGIN-Architecture.md)
- [NGIN Platform Direction](/home/berggrenmille/NGIN/docs/architecture/NGIN-Platform-Direction.md)
- [NGIN.Core Application Model Draft](/home/berggrenmille/NGIN/docs/api-drafts/NGIN.Core-ApplicationModel.md)
- [Spec 002: Application Host and Builder Model](/home/berggrenmille/NGIN/docs/specs/002-runtime-kernel-design.md)
- [Spec 003: Package, Module, and Plugin Model](/home/berggrenmille/NGIN/docs/specs/003-plugin-abi-header-spec.md)
- [Spec 004: Editor and Product Architecture](/home/berggrenmille/NGIN/docs/specs/004-editor-architecture.md)
- [Spec 005: Platform Transition and Next Steps](/home/berggrenmille/NGIN/docs/specs/005-implementation-roadmap.md)
