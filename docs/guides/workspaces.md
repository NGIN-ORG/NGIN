# Workspaces

A `.ngin` workspace is an optional repository-level model. It lists projects,
package sources, shared defaults, profiles, and platform or toolchain
definitions.

```xml
<Workspace SchemaVersion="4"
           Name="Game"
           DefaultProfile="dev">
  <Projects>
    <Project Path="Engine/Engine.nginproj" />
    <Project Path="App/App.nginproj" />
  </Projects>

  <Defaults>
    <OutputRoot Path="build/ngin" />
    <Language Standard="C++23" Required="true" Extensions="false" />
    <Backend Name="CMake" Mode="Generated" />
    <HostPlatform Name="host" />
    <TargetPlatform Name="host" />
  </Defaults>

  <Packages>
    <Source Name="local" Path="Packages" />
    <PackageProvider Name="Game.Engine" Root="Engine" />
  </Packages>

  <Profiles>
    <Profile Name="dev">
      <Defaults>
        <Optimization Mode="Off" />
        <DebugSymbols Enabled="true" />
        <LinkTimeOptimization Enabled="false" />
      </Defaults>
    </Profile>
  </Profiles>
</Workspace>
```

Projects do not require a workspace. When one exists, the CLI discovers it by
walking ancestor directories. Pass `--workspace <file.ngin>` to select an exact
manifest.

Useful commands:

```bash
ngin workspace list
ngin workspace status
ngin workspace doctor
```

Workspace profiles provide shared policy. A project's own profile remains the
closest authoring scope and can refine inherited named items.

See the [workspace manifest reference](../reference/workspace-manifest.md).
