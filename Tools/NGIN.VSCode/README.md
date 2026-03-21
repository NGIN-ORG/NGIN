# NGIN Tools

`NGIN Tools` is the in-repo VS Code extension for working with NGIN projects from the editor without leaving the CLI-first model behind. It exists so contributors can use build, validate, graph, run, and debug flows inside VS Code while still going through the same project/configuration surface that the native `ngin` command uses.

The extension is not a second build system. It is a front end over the authored NGIN model and the native CLI that already lives in this repo.

## What It Integrates With

The extension is built around a few concrete repo pieces:

- the native CLI in `Tools/NGIN.CLI`
- workspace and project manifests under the repo root and `Examples/`
- generated staged output and `.nginlaunch`
- `ms-vscode.cpptools` for C/C++ debug and configuration-provider support

That makes it contributor-oriented by design. It is most useful when you are already working inside this repo and want the editor to understand the same selected project and configuration that the CLI understands.

## Main Features

The extension currently provides:

- a dedicated `NGIN` activity-bar view with workspace overview and project/configuration navigation
- bottom status bar items for workspace, project, configuration, build, run, and debug
- project and configuration selection from workspace manifests
- `validate`, `graph`, `build`, `run`, and `debug` commands backed by the CLI
- generated `.nginlaunch` resolution for run and debug
- auto-provided VS Code tasks for build, validate, and graph
- a custom `ngin` debug type that resolves to native C/C++ debug sessions
- custom C/C++ configuration-provider support for `ms-vscode.cpptools` backed by staged compile databases
- snippets and file registration for `.ngin`, `.nginproj`, `.nginpkg`, and `.nginlaunch`
- optional validate-on-save

## How It Maps To The V2 CLI

The editor surface mirrors the CLI directly:

- selected project in the extension maps to `--project`
- selected configuration in the extension maps to `--configuration`
- build uses `ngin build`
- validation uses `ngin validate`
- graph inspection uses `ngin graph`
- run/debug resolve from the staged `.nginlaunch`

That is the intended contract. If you understand the terminal command, the editor should feel familiar instead of inventing new concepts.

## Build and Install

This extension expects Node.js and npm to be installed locally.

Install dependencies:

```bash
cd Tools/NGIN.VSCode
npm ci
```

Build the extension bundle:

```bash
npm run build
```

Package and install it locally:

```bash
VERSION=$(node -p "require('./package.json').version")
npx @vscode/vsce package --out "ngin-tools-${VERSION}.vsix"
code --install-extension "./ngin-tools-${VERSION}.vsix" --force
```

The repo may also already contain a checked-in `.vsix` built from the current package version. That is useful when you just want to install the extension quickly and do not need to rebuild it first.

## Development Host Workflow

Run unit tests:

```bash
npm run test:unit
```

Run integration tests:

```bash
npm run test:integration
```

For active extension development, open `Tools/NGIN.VSCode` in VS Code and launch the extension host target from the extension debugging workflow. The checked-in launch configuration under `.vscode/launch.json` opens the repository root as the test workspace and uses the extension build step before starting the host.

## Debug, Tasks, and Configuration Behavior

The custom `ngin` debug type expects:

- `project`
- `configuration`

When a launch is requested, the extension either reuses an existing staged `.nginlaunch` or triggers `ngin build` first, depending on the debug configuration and what is already present on disk.

Task generation follows the same rule. The extension enumerates known projects and configurations from the workspace, then creates build, validate, and graph tasks for those combinations rather than inventing editor-only task definitions by hand.
