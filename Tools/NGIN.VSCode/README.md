# NGIN Tools

`NGIN Tools` is the in-repo VS Code extension for NGIN contributors.

It replaces the old CMake Tools-centric workflow with a CLI-first flow built around:

- `ngin project validate`
- `ngin project build`
- staged `.ngintarget` output
- direct staged executable run/debug

## Features

- project and variant selection from workspace manifests
- build, validate, graph, run, and debug commands
- auto-provided VS Code tasks for build, validate, and graph
- custom `ngin` debug type that resolves to native C/C++ debug sessions
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

Open the extension in VS Code and launch the `Extension` debug target from the VS Code extension host workflow.
