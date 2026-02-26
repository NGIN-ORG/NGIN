# Tools

Utilities for managing the umbrella `NGIN` workspace.

## `ngin-sync.py`

Manifest-driven helper for:

- listing pinned components
- showing local workspace status (root repos / externals / overrides)
- checking workspace health (`doctor`)
- syncing source checkouts into `workspace/externals`
- honoring local path overrides for active development

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

Windows note:

- use `py tools\\ngin-sync.py <command>` if `python` is not on `PATH`

### Current Layout (Chosen)

The current preferred local layout is root-level sibling component repos:

- `./NGIN.Base`
- `./NGIN.Reflection`
- `./NGIN.ECS`
- `./NGIN.Core`

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
