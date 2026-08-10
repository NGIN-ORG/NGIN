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

Analyze one translation unit and request the editor protocol with:

```text
ngin analyze --project Checked.App.nginproj --file src/main.cpp --format json --lock build/Checked.App/ngin.lock
```

The result is a stable `NGIN.ActionDiagnostics` envelope containing file,
range, severity, analyzer identity, rule code, message, and fix inventory.
Multiple resolved Analyze Actions contribute to the same envelope.

The VS Code extension asks once before enabling verified project tooling,
stores the lock in the selected output directory, and passes it automatically.
It analyzes the relevant translation unit when a C/C++ file is opened or saved,
publishes Problems and editor squiggles, cancels obsolete work, and offers
full-project analysis explicitly. A stale lock is never bypassed; the extension
offers to regenerate it through the normal package-lock command.
