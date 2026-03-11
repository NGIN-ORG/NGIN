# Examples

`Examples/` contains the canonical authored examples for the active NGIN model.

- `App.Basic/` is the smallest real application project in the repo. It owns its `main.cpp`, project manifest, config, and executable output.
- `Workspace/NGIN.Workspace.ngin` is a minimal sample workspace file that points at `App.Basic` and demonstrates the `.ngin` workspace layer.

These examples use the current split:

- `.ngin` for workspaces
- `.nginproj` for authored projects
- `.nginpkg` for reusable packages
