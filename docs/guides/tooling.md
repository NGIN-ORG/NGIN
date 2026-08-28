# Project tooling actions

Projects explicitly select analyzers and formatters:

```xml
<Executable Name="Checked.App">
  <Build><Source Include="src/**/*.cpp" /></Build>
  <Tooling>
    <Analyze Using="NGIN.Tooling.ClangTidy/Analyze" />
    <Format Using="NGIN.Tooling.ClangFormat/Format" />
  </Tooling>
</Executable>
```

Run analyzers with `ngin analyze` and source format actions with
`ngin tooling format`. The separate `ngin format` command canonicalizes XML
manifests. Tooling packages remain inert until selected; their backing Tools
resolve in host context and are subject to workspace trust and dependency-lock
policy. The VS Code extension publishes structured analyzer results into the
native Problems view.
