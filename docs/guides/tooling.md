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
