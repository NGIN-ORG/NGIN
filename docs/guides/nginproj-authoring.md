# Project authoring

A `.nginproj` describes one product directly:

```xml
<Project Name="App" Type="Application">
  <Dependencies><Package Name="NGIN.Base" Compatible="0.1" /></Dependencies>
  <Build><Source Include="src/**/*.cpp" /></Build>
  <Launch Name="default" Default="true"><Executable Product="App" /></Launch>
</Project>
```

There is no format number, product wrapper, generic Feature, Scope string, or
profile overlay. Use named exports for package components, typed Options for
product choices, Actions for tools, and workspaces for shared selection and
policy. The complete contract is in the [project manifest reference](../reference/project-manifest.md).
