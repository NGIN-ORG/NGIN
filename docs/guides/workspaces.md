# Workspaces

A workspace supplies discovery, package acquisition policy, named selections,
defaults, and convenience presets. It does not rewrite project semantics.

```xml
<Workspace Name="Example">
  <Projects><Project Include="apps/**/*.nginproj" /></Projects>
  <Configurations><Configuration Name="Debug"><Optimization Mode="Off" /></Configuration></Configurations>
  <Targets><Target Name="host" OS="host" Architecture="host" /></Targets>
  <Toolchains><Toolchain Name="default" Compiler="default" /></Toolchains>
  <Defaults><Configuration Name="Debug" /><Target Name="host" /><Toolchain Name="default" /></Defaults>
  <Packages>
    <Source Name="local" Kind="Directory" Path="Packages" />
    <LocalPackage Name="Example.Core" Manifest="Packages/Example.Core/Example.Core.nginpkg" Root="src/Example.Core" />
    <Version Name="Example.Core" Compatible="1" />
  </Packages>
  <Policies><PackageProviders><Allow Kind="Directory" /></PackageProviders></Policies>
</Workspace>
```

Lists use repeated elements. PackageProvider and compatibility allow-lists are
typed children, not encoded strings. See the [workspace reference](../reference/workspace-manifest.md).
