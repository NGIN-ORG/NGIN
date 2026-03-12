# NGIN Tools

`NGIN Tools` is the in-repo VS Code extension for NGIN contributors.

It replaces the old CMake Tools-centric workflow with a CLI-first flow built around:

- `ngin project validate`
- `ngin project build`
- staged `.ngintarget` output
- direct staged executable run/debug

## Features

- dedicated `NGIN` activity-bar view container with workspace overview and project/variant navigation
- core bottom status bar items for workspace, project, variant, build configuration, build, run, and debug
- project and variant selection from workspace manifests
- build configuration selection (`Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel`) from the command palette, sidebar, and bottom bar
- build, validate, graph, run, and debug commands
- auto-provided VS Code tasks for build, validate, and graph
- custom `ngin` debug type that resolves to native C/C++ debug sessions
- custom C/C++ configuration provider support for `ms-vscode.cpptools` backed by NGIN compile databases
- snippets and file registration for `.ngin*` manifests
- optional validate-on-save

## Development

This extension expects Node.js and npm to be installed locally.

Install dependencies:

```bash
cd Tools/NGIN.VSCode
npm install
```

Build:

```bash
npm run build
```

Run unit tests:

```bash
npm run test:unit
```

Run integration tests:

```bash
npm run test:integration
```

Open `Tools/NGIN.VSCode` in VS Code and launch the `Extension` debug target from the VS Code extension host workflow.
The checked-in launch configuration lives at `.vscode/launch.json`, runs `npm: build` before starting the extension host, and opens the repository root as the test workspace.
