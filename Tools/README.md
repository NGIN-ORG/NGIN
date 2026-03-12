# Tools

The workspace CLI is a native C++ executable built from [main.cpp](/home/berggrenmille/NGIN/Tools/NGIN.CLI/src/main.cpp).

It uses `NGIN.Base` for XML parsing and presents one workspace/project/package workflow:

1. author a `.ngin` workspace and `.nginproj` projects
2. `ngin project validate`
3. `ngin project graph`
4. `ngin project build`

`.ngintarget` is generated output, not a primary authored manifest.

## Commands

- `ngin workspace list`
- `ngin workspace status`
- `ngin workspace doctor`
- `ngin workspace sync`
- `ngin project validate --project <file.nginproj> --variant <name>`
- `ngin project graph --project <file.nginproj> --variant <name>`
- `ngin project build --project <file.nginproj> --variant <name> --output <dir>`
- `ngin package list`
- `ngin package show <Package>`

If `--project` is omitted, `ngin` walks upward from the current directory and uses the first `.nginproj` it finds.

## Active Workspace Files

- [NGIN.ngin](/home/berggrenmille/NGIN/NGIN.ngin)
- [NGIN.Workspace.ngin](/home/berggrenmille/NGIN/Examples/Workspace/NGIN.Workspace.ngin)
- [App.Basic.nginproj](/home/berggrenmille/NGIN/Examples/App.Basic/App.Basic.nginproj)
- [Examples README](/home/berggrenmille/NGIN/Examples/README.md)
- [Packages README](/home/berggrenmille/NGIN/Packages/README.md)
- [Dependencies README](/home/berggrenmille/NGIN/Dependencies/README.md)

Runtime module and plugin metadata are authored inside `.nginpkg` files. Package wrappers also declare source binding, exposed artifacts, and any backend-thin build hints needed so the umbrella workspace can expose first-party and third-party code through one package model.

The root `.ngin` file is the workspace authority. It declares package source roots and the projects available to the workspace. Package discovery comes from `<PackageSources>` in that workspace file rather than a separate catalog.

## Build Output

`ngin build` stages package contents and project config sources into an output directory and emits `<Project>.<Variant>.ngintarget` as the staged layout manifest.

Default staged output:

- `.ngin/build/<Variant>/`

## CMake Targets

- `ngin.validate`
- `ngin.graph.core`
- `ngin.build.core`
- `ngin.workflow`

## VS Code Extension

The workspace now also contains an in-tree VS Code extension at `Tools/NGIN.VSCode`.

It is contributor-focused and provides:

- NGIN project and variant selection
- `validate`, `build`, `run`, and `debug` flows driven by the `ngin` CLI
- staged `.ngintarget` launch resolution
- VS Code tasks and a custom `ngin` debug type

Extension development uses Node.js tooling and lives separately from the CMake-based native CLI build.
