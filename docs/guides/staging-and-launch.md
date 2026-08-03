# Staging and launch

NGIN builds into a staged directory containing the selected product and its
runtime files. This gives local runs, tests, editors, and publishing the same
layout.

## Stage files

```xml
<Application>
  <Stage>
    <Config Source="config/app.json" Target="config/app.json" />
    <Content Source="assets/**" Target="assets" />
  </Stage>
</Application>
```

Every staged item has an owner and target. Collisions fail by default. Use
`Collision="Override"` only when a closer authoring scope intentionally
replaces the same target.

Package features may also contribute runtime assets and notices. Those files
join the same collision-checked stage plan.

## Define launch behavior

```xml
<Launch Executable="$(OutputName)"
        WorkingDirectory="."
        Args="--config config/app.json" />
```

`ngin build` writes a generated `.nginlaunch` file into the staged output.
`ngin run`, `ngin test`, and `ngin benchmark` consume resolved launch
information rather than rereading source manifests at runtime.

```bash
ngin stage
ngin graph --stage-plan --format json
ngin graph --launch-plan --format json
ngin run -- --user-argument
```

Do not commit or hand-edit `.nginlaunch`; it is generated output.
