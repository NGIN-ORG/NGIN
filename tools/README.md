# Tools

Utilities for managing the umbrella `NGIN` workspace.

## `ngin-sync.py`

Manifest-driven helper for:

- listing pinned components
- showing local workspace status (root repos / externals / overrides)
- checking workspace health (`doctor`)
- syncing source checkouts into `workspace/externals`
- honoring local path overrides for active development
- validating Spec 001 architecture constraints (`validate-spec001`)
- resolving target module/plugin closure (`resolve-target`)

### Commands

- `python tools/ngin-sync.py list`
  - prints manifest components, versions, pins, and overrides
- `python tools/ngin-sync.py status`
  - prints where each component resolves from (`root`, `externals`, `override`, `none`)
  - shows local HEAD and whether it matches the manifest pin
- `python tools/ngin-sync.py doctor`
  - checks required tools (`git`, `cmake`, `python`)
  - validates manifest/override JSON
  - checks local component repo presence and pin availability
- `python tools/ngin-sync.py sync`
  - clones/fetches/checks out pinned refs into `workspace/externals`
  - skips components that are overridden or unpinned
- `python tools/ngin-sync.py validate-spec001`
  - validates JSON schemas + component graph + module graph + target composition + plugin compatibility
  - validates semver/version-range fields and canonical load phases
  - enforces pinned refs for `required: true` components on non-`dev` channels
  - performs best-effort static scan of local CMake dependency references
  - fails on violations (hard gate mode)
- `python tools/ngin-sync.py resolve-target --target <TargetName>`
  - resolves deterministic module/plugin closure for target packaging stages
  - fails if graph is invalid or target cannot be resolved
  - accepts `--target-dir <path>` for static-scan parity with `validate-spec001`

### JSON report output

Both `validate-spec001` and `resolve-target` support:

- `--json-report <path>`
  - writes a stable JSON report for CI artifact inspection

### Platform metadata files

Platform metadata and draft schemas live in `manifests/`:

- `module.schema.json`
- `project.schema.json`
- `plugin-bundle.schema.json`
- `package.schema.json`
- `target.schema.json`
- `module-graph.schema.json`
- `module-catalog.json`
- `plugin-catalog.json`
- `target-catalog.json`

Windows note:

- use `py tools\\ngin-sync.py <command>` if `python` is not on `PATH`

### Current Layout (Chosen)

The current preferred local layout is root-level sibling component repos:

- `./NGIN.Base`
- `./NGIN.Log`
- `./NGIN.Runtime`
- `./NGIN.Reflection`
- `./NGIN.ECS`

`status` and `doctor` prefer this layout automatically, while `sync` can still populate `workspace/externals/` for missing components.

### Local Overrides (untracked)

Create `.ngin/workspace.overrides.json`:

```json
{
  "paths": {
    "NGIN.Base": "/home/me/dev/NGIN.Base",
    "NGIN.Reflection": "/home/me/dev/NGIN.Reflection"
  }
}
```

This lets you develop against local repos without editing `manifests/platform-release.json`.
