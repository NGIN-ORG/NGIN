# Projects

One `.nginproj` describes one primary product directly. Choose `Application`,
`Library`, `Tool`, `Test`, `Benchmark`, `Plugin`, or `External` with the root
`Type` attribute. There is no generic Module product: linked reusable code is a
Library, dynamically loaded code is a Plugin, and C++ modules are Build items.

```xml
<Project Name="Math" Type="Library" Linkage="Static">
  <Build>
    <Language Standard="C++23" Required="true" Extensions="false" />
    <Source Include="src/**/*.cpp" />
    <Header Include="include/**/*.hpp" Visibility="Public" />
    <IncludeDirectory Path="include" Visibility="Public" />
    <Define Name="MATH_BUILDING" Value="1" Visibility="Private" />
  </Build>
</Project>
```

Build declarations are additive and have stable identities. `Include`,
`Exclude`, `Remove`, and `Update` are explicit operations. Product differences
use typed Options and narrowly matched Refinements over Configuration, Target,
or Toolchain facts—there is no general condition language.

Dependencies name packages or projects. Package `<Use>` children activate
specific Libraries, Tools, Actions, Plugins, or Assets. Testing and publishing
dependencies live in their own semantic sections.

Use `ngin validate`, `ngin graph`, and `ngin explain <kind>:<identity>` to see
the resolved result and provenance. See the [project manifest reference](../reference/project-manifest.md).
