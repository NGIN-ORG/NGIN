# NGIN tools

`Tools/` contains the native CLI, the VS Code extension, and optional bundled
CMake and Ninja payloads. The CLI is the source of truth; editor features call
the same commands and consume the same Composition Graph.

## Build the CLI

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
```

The executable is written to `build/dev/Tools/NGIN.CLI/ngin` or
`ngin.exe` on Windows.

## Daily workflow

```bash
ngin validate
ngin build
ngin run
```

Use `ngin graph` when you need to understand the resolved project, or
`ngin inspect --format json` for machine-readable output.

```bash
ngin configure   # generate backend metadata without building
ngin publish     # create a configured distribution
```

Run `ngin` without arguments for the complete command list, or see the
[CLI reference](../docs/reference/cli.md).

## Build output

The default workspace output is:

```text
build/ngin/<Project>/<Configuration>-<Target>-<Toolchain>/
```

`ngin build` creates the selected product. `ngin stage` derives and executes a
StagePlan for product artifacts, runtime files, assets, plugins, and notices.
`ngin run` derives a LaunchPlan directly from the same resolved graph.

Projects can run without a workspace. Use `--project` to select a project and
`--workspace` when workspace discovery, package policy, or named selection is
required.

## Backend tools

Generated builds currently use CMake with Ninja. Both executables must be
available on `PATH`; publishing additionally requires CPack.

## VS Code

[`NGIN.VSCode`](NGIN.VSCode) provides graph-driven Solution and Active Project
views, selection, lifecycle tasks and commands, staged native debugging,
compile-database-backed C++ IntelliSense, a resolved project dashboard, safe
project-file authoring, and direct XML completion, formatting, validation, and
schema metadata. The CLI and Composition Graph remain the semantic authority;
the extension does not contain a second resolver.
