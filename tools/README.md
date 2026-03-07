# Tools

The workspace CLI is a native C++ executable built from [ngin.cpp](/home/berggrenmille/NGIN/tools/ngin.cpp).

It uses `NGIN.Base` for XML parsing and presents one package-first workflow:

1. author `.nginproj` and `.nginpkg` files
2. `validate`
3. `graph`
4. `build`

`.ngintarget` is generated output, not a primary authored manifest.

## Commands

- `ngin list`
- `ngin status`
- `ngin doctor`
- `ngin sync`
- `ngin validate --project <file.nginproj> --target <Target>`
- `ngin graph --project <file.nginproj> --target <Target>`
- `ngin build --project <file.nginproj> --target <Target> --output <dir>`
- `ngin package list`
- `ngin package show <Package>`

If `--project` is omitted, `ngin` walks upward from the current directory and uses the first `.nginproj` it finds.

## Active Metadata Files

- [platform-release.xml](/home/berggrenmille/NGIN/manifests/platform-release.xml)
- [package-catalog.xml](/home/berggrenmille/NGIN/manifests/package-catalog.xml)
- [NGIN.Workspace.nginproj](/home/berggrenmille/NGIN/manifests/NGIN.Workspace.nginproj)

Runtime module and plugin metadata are authored inside `.nginpkg` files. Lower-level runtime descriptors may still exist internally inside `NGIN.Core`, but they are not part of the normal workspace authoring flow.

## Build Output

`ngin build` stages package contents and project config sources into an output directory and emits `<Target>.ngintarget` as the staged layout manifest.

Default staged output:

- `.ngin/build/<Target>/`

## CMake Targets

- `ngin.validate`
- `ngin.graph.core`
- `ngin.build.core`
- `ngin.workflow`
