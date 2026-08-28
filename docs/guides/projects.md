# Authoring projects

Choose the physical output first:

```xml
<Executable Name="App">
  <Build><Source Include="src/**/*.cpp" /></Build>
</Executable>
```

```xml
<Library Name="Math" Kind="Static">
  <Build>
    <Source Include="src/**/*.cpp" />
    <Header Include="include/**/*.hpp" Visibility="Public" />
    <IncludeDirectory Path="include" Visibility="Public" />
  </Build>
</Library>
```

An Executable has an implicit Run. Add Run only to customize arguments,
environment, or working directory. Add Test or Benchmark registrations when a
runner should execute the product. Build a loadable artifact with
`Library Kind="Plugin"`; runtime loading remains application-owned.

Put package, project, and capability requirements under Uses. Use shallow When
blocks for platform-specific additions. See the complete
[project manifest reference](../reference/project-manifest.md).
