# Workspace manifest reference

A `.ngin` workspace is optional. Its root is:

```xml
<Workspace SchemaVersion="4"
           Name="WorkspaceName"
           DefaultProfile="dev">
</Workspace>
```

## Sections

| Section | Purpose |
| --- | --- |
| `Imports` | Definition fragments for shared platform and toolchain data |
| `Projects` | Project manifests in the workspace |
| `Defaults` | Output root, language, backend, host, and target defaults |
| `Packages` | Package sources, version policy, and source providers |
| `Platforms` | Named platform definitions |
| `Toolchains` | Named toolchain definitions |
| `Profiles` | Shared defaults and product-kind overlays |

Example:

```xml
<Workspace SchemaVersion="4" Name="App" DefaultProfile="dev">
  <Projects>
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
    <Version Name="NGIN.Base" Range=">=0.1.0 &lt;0.2.0" />
    <PackageProvider Name="NGIN.Base" Root="Dependencies/NGIN/NGIN.Base" />
  </Packages>
</Workspace>
```

The CLI discovers a workspace through ancestor directories or accepts an exact
path through `--workspace`. Project selection remains explicit when more than
one project is applicable.

Run `ngin schema --format json` for the current executable schema and
`ngin workspace doctor` for repository-specific diagnostics.
