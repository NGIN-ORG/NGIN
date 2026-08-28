# `.nginproj` authoring

A project manifest has an `Executable` or `Library` root and directly contains
Uses, Build, Generate, Tooling, Stage, Run, Test, Benchmark, Publish, Options,
and additive When sections as applicable.

```xml
<Executable Name="App">
  <Uses><Package Name="NGIN.Base" Version="0.1" /></Uses>
  <Build><Source Include="src/**/*.cpp" /></Build>
</Executable>
```

Run `ngin validate`, `ngin format --check`, and
`ngin inspect --effective` while authoring. The precise grammar is documented
in [project-manifest.md](../reference/project-manifest.md).
