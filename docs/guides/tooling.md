# Tooling Actions

Packages export host Tools and explicit Actions. Merely depending on a package
never runs its executable.

```xml
<Project Name="Checked.App" Type="Application">
  <Dependencies>
    <Package Name="NGIN.Tooling.ClangTidy" Compatible="0.1">
      <Use Action="Analyze" />
    </Package>
  </Dependencies>
  <Tooling><Analyze Action="NGIN.Tooling.ClangTidy::Analyze" /></Tooling>
</Project>
```

An Action declares its Tool, typed inputs and outputs, arguments, environment,
and determinism in the package manifest. NGIN resolves the Tool on a host
PackageInstance and checks workspace trust before execution. Use `<Generate>`
for generation and `<Analyze>`, `<Format>`, `<Validate>`, or `<Custom>` inside
`<Tooling>` for the other Action kinds.

When workspace Action policy requires locked PackageInstances, create and pass
the dependency lock explicitly:

```text
ngin package lock --project Checked.App.nginproj --output build/Checked.App/ngin.lock
ngin analyze --project Checked.App.nginproj --lock build/Checked.App/ngin.lock
```

The VS Code extension exposes **NGIN: Lock Dependencies**, stores the lock in
the active output directory, and passes it automatically to Analyze and Format.
