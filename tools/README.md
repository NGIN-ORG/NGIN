# Tools

The workspace CLI is a native C++ executable built from [ngin.cpp](/home/berggrenmille/NGIN/tools/ngin.cpp).

It uses `NGIN.Base` for XML parsing and presents one package-first workflow:

1. author `.nginproj` and `.nginpkg` files
2. `validate`
3. `graph`
4. `build`

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
- [module-catalog.xml](/home/berggrenmille/NGIN/manifests/module-catalog.xml)
- [plugin-catalog.xml](/home/berggrenmille/NGIN/manifests/plugin-catalog.xml)
- [NGIN.Workspace.nginproj](/home/berggrenmille/NGIN/manifests/NGIN.Workspace.nginproj)

Lower-level runtime descriptor example:

- [demo.module.xml](/home/berggrenmille/NGIN/docs/examples/runtime-descriptors/DemoPlugin/demo.module.xml)

## Build Output

`ngin build` stages package contents and project config sources into an output directory and emits `<Target>.ngintarget` as the staged layout manifest.

Default staged output:

- `.ngin/build/<Target>/`

## CMake Targets

- `ngin.validate`
- `ngin.graph.core`
- `ngin.build.core`
- `ngin.workflow`
