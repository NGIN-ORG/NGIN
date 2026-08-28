# Workspaces

A workspace discovers authored inputs and centralizes policy:

```xml
<Workspace Name="Example">
  <Discover>
    <Projects Include="apps/**/*.nginproj" />
    <Packages Include="Packages/**/*.nginpkg" />
  </Discover>
  <Versions><Package Name="Example.Core" Version="1" /></Versions>
  <Profiles Default="dev"><Profile Name="dev" Configuration="Debug" /></Profiles>
</Workspace>
```

Use Versions for shared constraints, Capabilities/Prefer for deterministic
implementation preferences, and Trust/AllowActions for explicit action trust.
Local package roots are inferred from discovered manifests. See
[workspace-manifest.md](../reference/workspace-manifest.md).
