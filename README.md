# NGIN

NGIN is the **umbrella workspace** for the NGIN platform.

This repo is mainly for:
- platform architecture/specs
- compatibility and release manifests
- workspace tooling for syncing/checking component repos

It is **not** the runtime/game/editor SDK itself and does not produce a single `NGIN` library.

## What You Should Do Here

If you want to use platform libraries in an app:
- Use component repos/packages directly (`NGIN.Base`, `NGIN.Log`, `NGIN.Runtime`, `NGIN.Reflection`, etc.).
- Treat this repo as architecture/release metadata, not as your app dependency.

If you are maintaining the platform:
- Update `manifests/platform-release.json` and spec docs.
- Run workspace validation/gates.

If you are developing multiple NGIN components together:
- Use this workspace to keep repos aligned and verify cross-repo compatibility.

## Quick Start

Use `python3` (or `py` on Windows).

1. Show platform-pinned components:

```bash
python3 tools/ngin-sync.py list
```

2. Check local workspace health:

```bash
python3 tools/ngin-sync.py doctor
python3 tools/ngin-sync.py status
```

3. Sync missing pinned repos into `workspace/externals`:

```bash
python3 tools/ngin-sync.py sync
```

4. Configure workspace helper project:

```bash
cmake --preset dev
```

5. Run Spec 001 architecture gates:

```bash
python3 tools/ngin-sync.py validate-spec001
python3 tools/ngin-sync.py resolve-target --target NGIN.RuntimeSample
```

## Common Workflows

Platform release/compatibility update:
- Edit [platform-release.json](/home/berggrenmille/NGIN/manifests/platform-release.json).
- Run `python3 tools/ngin-sync.py validate-spec001`.
- Confirm target resolution for baseline targets.

Cross-repo local development:
- Keep sibling repos in this workspace root (`NGIN.Base`, `NGIN.Log`, ...), or use `.ngin/workspace.overrides.json`.
- Run `status`, `doctor`, and `validate-spec001` after dependency changes.

CI parity check locally:
- `cmake --preset dev`
- `cmake --build build/dev --target ngin.spec001.validate`
- `cmake --build build/dev --target ngin.spec001.resolve.runtime`

## Repository Map

- `docs/architecture/`: umbrella architecture docs
- `docs/specs/`: normative specs (`001`, `002`, ...)
- `manifests/`: platform compatibility + module/plugin/target metadata
- `tools/`: workspace tooling (`ngin-sync.py`)
- `cmake/`: workspace CMake integration helpers

## Required Platform Components (Current)

- `NGIN.Base`
- `NGIN.Log`
- `NGIN.Runtime`
- `NGIN.Reflection`

## Spec 001 Enforcement Surface

- Metadata catalogs:
  - `manifests/module-catalog.json`
  - `manifests/plugin-catalog.json`
  - `manifests/target-catalog.json`
- Metadata schemas:
  - `manifests/module.schema.json`
  - `manifests/plugin-bundle.schema.json`
  - `manifests/target.schema.json`
  - `manifests/module-graph.schema.json`
- CLI gates:
  - `python3 tools/ngin-sync.py validate-spec001`
  - `python3 tools/ngin-sync.py resolve-target --target <TargetName>`
- CMake helper targets:
  - `ngin.spec001.validate`
  - `ngin.spec001.resolve.runtime`

## Read Next

- [NGIN Architecture](/home/berggrenmille/NGIN/docs/architecture/NGIN-Architecture.md)
- [Spec 001: Module Dependency Graph](/home/berggrenmille/NGIN/docs/specs/001-module-dependency-graph.md)
- [Spec 002: Runtime Kernel Design](/home/berggrenmille/NGIN/docs/specs/002-runtime-kernel-design.md)
