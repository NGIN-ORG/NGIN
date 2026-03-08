# Tools

The workspace CLI is a native C++ executable built from [main.cpp](/home/berggrenmille/NGIN/Tools/NGIN.CLI/src/main.cpp).

It uses `NGIN.Base` for XML parsing and presents one package-first workflow:

1. author `.nginproj` and `.nginpkg` files
2. `ngin project validate`
3. `ngin project graph`
4. `ngin project build`

`.ngintarget` is generated output, not a primary authored manifest.

## Commands

- `ngin workspace list`
- `ngin workspace status`
- `ngin workspace doctor`
- `ngin workspace sync`
- `ngin project validate --project <file.nginproj> --target <Target>`
- `ngin project graph --project <file.nginproj> --target <Target>`
- `ngin project build --project <file.nginproj> --target <Target> --output <dir>`
- `ngin package list`
- `ngin package show <Package>`

If `--project` is omitted, `ngin` walks upward from the current directory and uses the first `.nginproj` it finds.

## Active Metadata Files

- [platform-release.xml](/home/berggrenmille/NGIN/manifests/platform-release.xml)
- [package-catalog.xml](/home/berggrenmille/NGIN/manifests/package-catalog.xml)
- [NGIN.Workspace.nginproj](/home/berggrenmille/NGIN/manifests/NGIN.Workspace.nginproj)
- [Packages README](/home/berggrenmille/NGIN/Packages/README.md)
- [Dependencies README](/home/berggrenmille/NGIN/Dependencies/README.md)

Runtime module and plugin metadata are authored inside `.nginpkg` files. Package wrappers also declare their source binding and build backend metadata so the umbrella workspace can expose first-party and third-party code through one package model.

## Build Output

`ngin build` stages package contents and project config sources into an output directory and emits `<Target>.ngintarget` as the staged layout manifest.

Default staged output:

- `.ngin/build/<Target>/`

## CMake Targets

- `ngin.validate`
- `ngin.graph.core`
- `ngin.build.core`
- `ngin.workflow`
