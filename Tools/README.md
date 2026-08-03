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
ngin clean       # remove NGIN-owned output for the selection
ngin rebuild     # clean, then build
ngin publish     # create a configured distribution
```

Run `ngin` without arguments for the complete command list, or see the
[CLI reference](../docs/reference/cli.md).

## Build output

The default workspace output is:

```text
build/ngin/<Project>/<Profile>/
```

`ngin build` places built products, runtime files, package contributions, and a
generated `.nginlaunch` file in the staged output. `ngin run` and the editor
use that launch information.

Projects can run without a workspace. Use `--project` to select a project and
`--workspace` when an exact workspace policy must be pinned.

## Backend tools

Generated builds currently use CMake and prefer Ninja. The CLI resolves backend
tools from:

1. explicit overrides such as `NGIN_CMAKE`, `NGIN_CPACK`, `NGIN_NINJA`, or
   `NGIN_THIRD_PARTY_TOOLS_ROOT`;
2. installed or repository-bundled payloads;
3. `PATH`.

Fetch the pinned payload for the current host with:

```bash
Tools/scripts/fetch-bundled-tools.sh
```

PowerShell:

```powershell
Tools\scripts\fetch-bundled-tools.ps1
```

Versions, upstream URLs, and checksums live in
[`ThirdParty/BuildTools/toolchains.json`](ThirdParty/BuildTools/toolchains.json).
Keep the accompanying
[third-party notices](ThirdParty/BuildTools/notices/THIRD_PARTY_TOOLS.md) with
any redistributed payload.

## Local settings and secrets

```bash
ngin settings init
ngin variables explain
```

Local settings are written under `.ngin/local/` and ignored by source control.
Secret values are redacted from normal CLI, graph, and launch output.

## VS Code

[`NGIN.VSCode`](NGIN.VSCode) adds project and profile selection, build and run
commands, graph-backed project views, manifest editing, native debugging, and
package-provided tooling to VS Code.
