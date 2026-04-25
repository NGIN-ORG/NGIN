# Tools

`Tools/` contains the contributor-facing tooling layer for the authored NGIN model. In practice that means two things: the native `ngin` CLI and the in-repo VS Code extension that drives the same workflows from the editor.

The important part is that both tools now speak the same V2 model. They operate on projects and configurations, they stage output into a predictable directory, and they treat `.nginlaunch` as generated tooling metadata instead of inventing a parallel editor-only flow.

## Native CLI Overview

The workspace CLI is the native C++ executable built from [main.cpp](/home/berggrenmille/NGIN/Tools/NGIN.CLI/src/main.cpp). It loads workspaces, projects, and packages, resolves the selected project configuration, and then validates, graphs, builds, or runs the staged result.

The normal authored loop is:

1. author or open a `.nginproj`
2. choose a configuration
3. `ngin validate`
4. `ngin graph`
5. `ngin build`
6. `ngin clean` or `ngin rebuild` when you want to reset generated artifacts for that scope
7. `ngin run`

If `--project` is omitted, the CLI can work from the current directory and discover the nearest project where that flow is supported.

## Bundled Build Backend Tools

The generated project build path uses CMake, and it prefers Ninja when Ninja is available. NGIN can use bundled third-party copies from `Tools/ThirdParty/BuildTools` so contributors and release users do not need CMake or Ninja preinstalled on the system.

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

The CLI resolves build backend tools in this order:

1. explicit environment override: `NGIN_CMAKE`, `NGIN_NINJA`, or `NGIN_THIRD_PARTY_TOOLS_ROOT`
2. bundled tools under `Tools/ThirdParty/BuildTools`
3. tools available on `PATH`

Pinned tool versions and upstream URLs live in `Tools/ThirdParty/BuildTools/toolchains.json`. License and notice requirements live in `Tools/ThirdParty/BuildTools/notices/THIRD_PARTY_TOOLS.md`. Keep the upstream license files inside each extracted payload when publishing bundled tool archives.

## Command Surface By Task

Workspace and package inspection:

- `ngin workspace list`
- `ngin workspace status`
- `ngin workspace doctor`
- `ngin package list`
- `ngin package show <Package>`

Project-level workflows:

- `ngin validate --project <file.nginproj> --configuration <name>`
- `ngin graph --project <file.nginproj> --configuration <name>`
- `ngin clean --project <file.nginproj> --configuration <name> --output <dir>`
- `ngin build --project <file.nginproj> --configuration <name> --output <dir>`
- `ngin rebuild --project <file.nginproj> --configuration <name> --output <dir>`
- `ngin run --project <file.nginproj> --configuration <name> --output <dir>`

Those commands all resolve the same authored concepts. The difference is whether you want a report, a graph, a staged build, or an actual process launch.

## Build Output and `.nginlaunch`

`ngin build` stages project output, package contents, and config sources into one output directory. It also emits a launch manifest named `<Project>.<Configuration>.nginlaunch`.

That generated file is the bridge between CLI/editor tooling and the staged output. It records:

- which project and configuration were selected
- which executable was chosen
- which runtime files were staged
- what working directory and environment the launch expects

Default staged output lives under:

- `.ngin/build/<Project>/<Configuration>/`

That means a normal local build of `App.Basic` with configuration `Runtime` lands in:

- `.ngin/build/App.Basic/Runtime/`

## VS Code Extension Overview

The repo also contains an in-tree VS Code extension at `Tools/NGIN.VSCode`. It is built around the CLI rather than around a separate editor-specific build model.

It provides:

- workspace, project, and configuration selection
- build, clean, rebuild, validate, graph, run, and debug commands
- `.nginlaunch`-based run and debug resolution
- editor tasks and a custom `ngin` debug type
- C/C++ compile database discovery for `ms-vscode.cpptools`

The intent is simple: if a command works in the terminal, the editor should be driving the same command and the same staged output.

## Typical Local Workflow

Configure and build the native CLI first:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
```

Validate the canonical example:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/App.Basic/App.Basic.nginproj \
  --configuration Runtime
```

Build a staged output:

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.Basic/App.Basic.nginproj \
  --configuration Runtime \
  --output build/manual/App.Basic
```

Run it:

```bash
./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/App.Basic/App.Basic.nginproj \
  --configuration Runtime \
  --output build/manual/App.Basic
```

If you prefer the editor workflow after that, open the repo in VS Code and use the extension’s project/configuration selection together with the same build and debug flows.
