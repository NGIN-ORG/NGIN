# Examples

`Examples/` contains the canonical authored examples for the active NGIN model.

- `App.Basic/` is the smallest real application project in the repo. It owns its `main.cpp`, project manifest, config, and executable output.
- `App.Showcase/` is a richer application example that demonstrates multi-variant project authoring, variant-level package composition, variant config overlays, and project-owned runtime module toggles.
- `Workspace/NGIN.Workspace.ngin` is a minimal sample workspace file that points at both example projects and demonstrates the `.ngin` workspace layer.

These examples use the current split:

- `.ngin` for workspaces
- `.nginproj` for authored projects
- `.nginpkg` for reusable packages
