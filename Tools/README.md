# Tools

`Tools/` contains the NGIN tooling layer used to work with authored projects:

- the native `ngin` CLI
- the in-repo VS Code extension
- optional bundled CMake/Ninja payload metadata and fetch scripts

Both the CLI and the VS Code extension use the same workspace, project, package,
profile, staged-output, and `.nginlaunch` model. The CLI is the source of
truth; the extension is an editor front end over that CLI behavior.

For platform concepts and the first-run flow, start with the root
[`README.md`](../README.md).

## Build the CLI

Prerequisites for building this repository:

- CMake 3.20 or newer
- Ninja
- a C++23-capable compiler

Configure the workspace and build the native CLI:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
```

The built CLI is:

```text
build/dev/Tools/NGIN.CLI/ngin
```

## Typical CLI Flow

The normal project loop is:

1. choose a `.nginproj`
2. choose a profile
3. validate the selected composition
4. inspect the graph when needed
5. configure generated build metadata when needed
6. build a staged output directory
7. run from the generated `.nginlaunch`

Minimal example:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --configuration Debug

./build/dev/Tools/NGIN.CLI/ngin configure \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --configuration Debug \
  --output build/manual/Hello.Native

./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --configuration Debug \
  --output build/manual/Hello.Native

./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --configuration Debug \
  --output build/manual/Hello.Native
```

Use `--configuration Debug`, `Release`, `RelWithDebInfo`, or `MinSizeRel` for
classic build-type selection. Use `--profile <name>` for custom product
scenarios such as runtime/editor/shipping profiles.

Use `clean` or `rebuild` when you need to reset generated artifacts for the
selected project/profile/output scope.

## CLI Commands

Project commands:

- `ngin validate`
- `ngin graph`
- `ngin configure`
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
- `ngin settings init`
- `ngin variables explain`

For the complete active command contract, see
[`../docs/specs/006-cli-contract.md`](../docs/specs/006-cli-contract.md).

## Build Backend Tools

NGIN currently generates backend input for CMake and prefers Ninja when Ninja is
available.

The CLI resolves backend tools in this order:

1. explicit environment overrides: `NGIN_CMAKE`, `NGIN_NINJA`, or
   `NGIN_THIRD_PARTY_TOOLS_ROOT`
2. bundled tools under `Tools/ThirdParty/BuildTools`
3. tools available on `PATH`

The bundled tools are for CLI-driven generated builds. Building the NGIN CLI
itself from this checkout still requires enough system tooling to run
`cmake --preset dev`.

## Bundled Build Tools

Bundled CMake and Ninja payloads are optional generated files. They are kept out
of normal git history, but can be fetched when a checkout or release package
needs local backend tools for generated project builds.

Fetch the current host payload:

```bash
Tools/scripts/fetch-bundled-tools.sh
```

On Windows without a POSIX shell:

```powershell
Tools\scripts\fetch-bundled-tools.ps1
```

Fetch all currently pinned host payloads:

```bash
Tools/scripts/fetch-bundled-tools.sh --all
```

Pinned versions and upstream URLs live in
[`ThirdParty/BuildTools/toolchains.json`](ThirdParty/BuildTools/toolchains.json).
License and notice requirements live in
[`ThirdParty/BuildTools/notices/THIRD_PARTY_TOOLS.md`](ThirdParty/BuildTools/notices/THIRD_PARTY_TOOLS.md).

Keep upstream license files inside each extracted payload when publishing
bundled tool archives.

Installed bundled distributions keep the same payload structure under
`share/ngin/tools`. The CLI resolves that executable-relative location before
workspace-local bundled tools and `PATH`; `NGIN_CMAKE`, `NGIN_CPACK`,
`NGIN_NINJA`, and `NGIN_THIRD_PARTY_TOOLS_ROOT` remain explicit overrides.

The CLI itself is also a V4 Tool product at `NGIN.CLI/NGIN.CLI.nginproj`.
Release profiles can publish thin or bundled ZIP/MSI/TGZ/DEB artifacts through
`ngin publish`.

Each release profile exposes only its corresponding thin or bundled publish
targets. Native Windows artifacts must be built on Windows and native Linux
artifacts must be built on Linux; the generated backend rejects foreign
operating-system or architecture targets before configuration.

## Staged Output

`ngin configure` prepares the generated CMake build tree and compile database
without building or staging artifacts.

`ngin build` produces a staged output directory containing:

- built artifacts
- resolved package contents
- runtime files
- a generated `.nginlaunch` file

Default location:

```text
.ngin/build/<Project>/<Profile>/
```

`ngin run` and the VS Code extension use `.nginlaunch` for local launch/debug
resolution. It is generated tooling metadata, not an authored input file.

## Local Settings And Secrets

Projects can declare environment variables that resolve from literal manifest
values, operating system environment variables, or explicitly imported local
settings. Local settings live in `.nginsettings` files and are intended for
machine-specific paths and secrets that should not be committed.

Create the default ignored local settings file:

```bash
ngin settings init --project Examples/Hello.Hosted/Hello.Hosted.nginproj
```

Inspect resolved variables for a profile:

```bash
ngin variables explain --project Examples/Hello.Hosted/Hello.Hosted.nginproj --profile Debug
```

Secret values are redacted in CLI output and are not written as raw values to
generated launch manifests.

## VS Code Extension

The extension in [`NGIN.VSCode`](NGIN.VSCode/) uses the native CLI as its
backend.

It provides:

- project and profile selection
- build, run, debug, validate, graph, clean, and rebuild commands
- `.nginlaunch`-based run and debug resolution
- C/C++ compile database discovery for `ms-vscode.cpptools`

The editor workflow mirrors the CLI workflow. See
[`NGIN.VSCode/README.md`](NGIN.VSCode/README.md) for build, install, and
extension-development details.
