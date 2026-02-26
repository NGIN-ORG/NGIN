# Tools

Utilities for managing the umbrella `NGIN` workspace.

## `ngin-sync.sh`

Manifest-driven helper for:

- listing pinned components
- syncing source checkouts into `workspace/externals`
- honoring local path overrides for active development

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

