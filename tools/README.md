# Tools

Utilities for managing the umbrella `NGIN` workspace.

## `ngin.py`

Primary platform CLI for:

- listing pinned components
- checking workspace status and health
- restoring projects into a local package lockfile and cache
- validating target composition from a `ngin.project.json`
- printing resolved package/module graphs
- producing deterministic target resolution reports

### Commands

- `python tools/ngin.py list`
- `python tools/ngin.py status`
- `python tools/ngin.py doctor`
- `python tools/ngin.py sync`
- `python tools/ngin.py package restore --project manifests/workspace.project.json`
- `python tools/ngin.py package list`
- `python tools/ngin.py package show NGIN.Core`
- `python tools/ngin.py build --project manifests/workspace.project.json --target <TargetName>`
- `python tools/ngin.py validate --project manifests/workspace.project.json --locked --target <TargetName>`
- `python tools/ngin.py graph --project manifests/workspace.project.json --locked --target <TargetName>`
- `python tools/ngin.py resolve --project manifests/workspace.project.json --locked --target <TargetName>`

Project-manifest flow:

- `python tools/ngin.py package restore --project docs/examples/project-model/ngin.project.json`
- `python tools/ngin.py build --project docs/examples/project-model/ngin.project.json --target Sandbox.Game`
- `python tools/ngin.py resolve --project docs/examples/project-model/ngin.project.json --locked`
- `python tools/ngin.py validate --project docs/examples/project-model/ngin.project.json --locked --target Tools.Cli`
- if `--project` is omitted, `ngin.py` walks upward from the current directory and uses the first `ngin.project.json` it finds

Restore and locked-mode options:

- `--lockfile <path>`
- `--cache-dir <path>`
- `--locked`
- `--package-override Package.Name=/path/to/ngin.package.json`
- `--package-root <path>`
- `--allow-package-probe`
- `--json-report <path>`

Modern JSON report shape:

- `ok`, `command`, `target`, `source`
- `validation`: stable validation summary for CI (`ok`, `counts`, `errors`, `warnings`, `scanSkipped`)
- `resolution`: stable package/module graph and warnings/errors payload
- `lockfile`: active lockfile path and source
- `cache`: active cache path and cache operation summary

Canonical local workflow:

1. `python tools/ngin.py package restore --project manifests/workspace.project.json`
2. `python tools/ngin.py build --project manifests/workspace.project.json --target NGIN.CoreSample`
3. `python tools/ngin.py validate --project manifests/workspace.project.json --locked --target NGIN.CoreSample`
4. `python tools/ngin.py graph --project manifests/workspace.project.json --locked --target NGIN.CoreSample`
5. `python tools/ngin.py resolve --project manifests/workspace.project.json --locked --target NGIN.CoreSample`

Workspace integration targets:

- `cmake --build build/dev --target ngin.package.restore`
- `cmake --build build/dev --target ngin.package.restore.report`
- `cmake --build build/dev --target ngin.build.core`
- `cmake --build build/dev --target ngin.build.core.report`
- `cmake --build build/dev --target ngin.validate.report`
- `cmake --build build/dev --target ngin.graph.core.report`
- `cmake --build build/dev --target ngin.resolve.report.core`
- `cmake --build build/dev --target ngin.reports`

Default report output directory:

- `build/dev/reports/`

### Platform metadata files

Platform metadata and draft schemas live in `manifests/`:

- `module.schema.json`
- `project.schema.json`
- `plugin-bundle.schema.json`
- `package.schema.json`
- `package-catalog.schema.json`
- `package-lock.schema.json`
- `module-catalog.json`
- `package-catalog.json`
- `plugin-catalog.json`
- `workspace.project.json`

### Cache Layout

Default local cache root:

- `.ngin/cache/`

Package metadata cache layout:

- `.ngin/cache/packages/<PackageName>/<Version>/ngin.package.json`
- `.ngin/cache/packages/<PackageName>/<Version>/entry.json`
- `.ngin/cache/packages/<PackageName>/<Version>/content/...`

Package manifests may now declare `contents.files[]` entries. `ngin package restore` copies those files into the package cache and records their hashes in both `entry.json` and `ngin.lock.json`.

Default project lockfile:

- `ngin.lock.json` next to the selected `ngin.project.json`

Default staged build output:

- `.ngin/build/<TargetName>/`

`ngin build` consumes the lockfile and cached package contents, stages all package payloads into the output directory using each content file's `targetPath` (or original path when omitted), copies any declared project `configSources`, and writes `ngin.target.json` as the staged target manifest.

### Current Layout (Chosen)

The current preferred local layout is root-level sibling component repos:

- `./NGIN.Base`
- `./NGIN.Log`
- `./NGIN.Core`
- `./NGIN.Reflection`
- `./NGIN.ECS`
- `./NGIN.Editor`

`status` and `doctor` prefer this layout automatically, while `sync` can still populate `workspace/externals/` for missing components.

### Local Overrides (untracked)

Create `.ngin/workspace.overrides.json`:

```json
{
  "paths": {
    "NGIN.Base": "/home/me/dev/NGIN.Base",
    "NGIN.Reflection": "/home/me/dev/NGIN.Reflection"
  },
  "packages": {
    "Samples.DemoPackage": "/home/me/dev/Samples.DemoPackage/ngin.package.json"
  }
}
```

This lets you develop against local repos without editing `manifests/platform-release.json`.
