# Tools

This directory contains the NGIN tooling layer used to work with authored projects:

- the native `ngin` CLI
- the in-repo VS Code extension

Both tools operate on the same workspace, project, package, and configuration model. This document focuses on how to build and use those tools as a contributor.

For the authored model and overall platform concepts, see the root [README.md](/home/berggrenmille/NGIN/README.md).

## Tooling Overview

NGIN tooling operates on:

- workspaces
- projects
- configurations
- staged output directories

The CLI and VS Code extension both resolve these concepts and execute the same underlying commands.

## Typical CLI Workflow

1. open or author a `.nginproj`
2. select a configuration
3. `ngin validate`
4. `ngin graph`
5. `ngin build`
6. `ngin run`

Use `clean` or `rebuild` when you need to reset generated artifacts.

## CLI Commands

Common project commands:

- `ngin validate`
- `ngin graph`
- `ngin build`
- `ngin run`
- `ngin clean`
- `ngin rebuild`

Workspace and package inspection:

- `ngin workspace list`
- `ngin workspace status`
- `ngin workspace doctor`
- `ngin package list`
- `ngin package show <Package>`

For full CLI behavior, see [docs/specs/006-cli-contract.md](/home/berggrenmille/NGIN/docs/specs/006-cli-contract.md).

## Build Backend

NGIN generates build input for CMake and prefers Ninja when Ninja is available.

The CLI resolves build backend tools in this order:

1. explicit environment overrides: `NGIN_CMAKE`, `NGIN_NINJA`, or `NGIN_THIRD_PARTY_TOOLS_ROOT`
2. bundled tools under `Tools/ThirdParty/BuildTools`
3. tools available on `PATH`

## Bundled Build Tools

Bundled CMake and Ninja payloads are optional generated files. They are kept out of normal git history, but can be fetched when a checkout or release package needs local build backend tools.

Fetch the current host payload:

```bash
Tools/scripts/fetch-bundled-tools.sh
```

On Windows without a POSIX shell, use:

```powershell
Tools\scripts\fetch-bundled-tools.ps1
```

Fetch all currently pinned host payloads from a POSIX shell:

```bash
Tools/scripts/fetch-bundled-tools.sh --all
```

Pinned tool versions and upstream URLs live in [toolchains.json](/home/berggrenmille/NGIN/Tools/ThirdParty/BuildTools/toolchains.json). License and notice requirements live in [THIRD_PARTY_TOOLS.md](/home/berggrenmille/NGIN/Tools/ThirdParty/BuildTools/notices/THIRD_PARTY_TOOLS.md).

Keep upstream license files inside each extracted payload when publishing bundled tool archives.

## Staged Output

`ngin build` produces a staged output directory containing:

- built artifacts
- resolved package contents
- runtime files
- a generated `.nginlaunch` file

Default location:

```text
.ngin/build/<Project>/<Configuration>/
```

The `.nginlaunch` file is used by tooling such as `ngin run` and VS Code debug integration. It is generated output, not an authored input.

## VS Code Extension

The extension in `Tools/NGIN.VSCode` uses the CLI as its backend.

It provides:

- project and configuration selection
- build, run, debug, validate, graph, clean, and rebuild commands
- `.nginlaunch`-based run and debug resolution
- C/C++ compile database discovery for `ms-vscode.cpptools`

The editor workflow mirrors the CLI workflow.

## Building The CLI

Configure the workspace and build the native CLI:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
```

## Minimal Example Flow

Validate, build, and run the canonical example:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/App.Basic/App.Basic.nginproj \
  --configuration Runtime

./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.Basic/App.Basic.nginproj \
  --configuration Runtime \
  --output build/manual/App.Basic

./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/App.Basic/App.Basic.nginproj \
  --configuration Runtime \
  --output build/manual/App.Basic
```
