# Workspace manifest reference

A `.ngin` file is an optional XML workspace. It discovers projects and owns
shared selection, package acquisition, defaults, trust, and reproducibility
policy. It does not add another build model: projects and packages remain the
semantic inputs, and CMake remains the only executable build adapter.

```xml
<Workspace Name="WorkspaceName">
  <Projects>
    <Project Include="Examples/**/*.nginproj"
             Exclude="Examples/Experimental/**/*.nginproj" />
  </Projects>
</Workspace>
```

There is no manifest format number before the first official release.

## Project discovery

Each `<Project>` uses exactly one discovery form:

```xml
<Projects>
  <Project Path="Apps/Gallery/Gallery.nginproj" />
  <Project Include="Libraries/**/*.nginproj"
           Exclude="Libraries/Archived/**/*.nginproj" />
</Projects>
```

`Path` names one file. `Include` is a portable workspace-relative glob and may
have one `Exclude` glob. Results are normalized, sorted, required to remain
inside the workspace, and followed through symlinks only when they remain
contained. Generated `.ngin` and `build` directories and version-control
metadata are pruned before traversal. A project matched by multiple declarations
is an error rather than being silently deduplicated.

## Build selection

Configurations, Targets, and Toolchains are deliberately distinct:

```xml
<Configurations>
  <Configuration Name="Debug">
    <Optimization Mode="Off" />
    <DebugSymbols Enabled="true" />
    <Option Name="Telemetry" Value="false" />
  </Configuration>
</Configurations>

<Targets>
  <Target Name="windows-x64" OS="windows" Architecture="x64">
    <Alias Name="desktop" />
  </Target>
</Targets>

<Toolchains>
  <Toolchain Name="msvc"
             Compiler="msvc"
             CompilerVersion="19.51"
             RuntimeLibrary="dynamic"
             Linker="link" />
</Toolchains>
```

A Target describes the destination platform. A Toolchain describes the tools
and ABI used to produce it. A Configuration describes build behavior. Typed
Options express real project/package choices. Presets are only named command
input expansions; their names never enter Composition Graph identity.

```xml
<Defaults>
  <OutputRoot Path="build/ngin" />
  <Configuration Name="Debug" />
  <Target Name="desktop" />
  <Toolchain Name="msvc" />
</Defaults>

<Presets>
  <Preset Name="dev" Command="build">
    <Configuration Name="Debug" />
    <Target Name="desktop" />
    <Toolchain Name="msvc" />
    <Option Name="Telemetry" Value="false" />
  </Preset>
</Presets>
```

## Packages and PackageProviders

`Source` declares an acquisition endpoint. `Binding` maps an NGIN package
coordinate to the provider-native coordinate. `LocalPackage` is the concise
source-tree form. `Version` is central policy and uses the same readable
constraint vocabulary as project dependencies.

```xml
<Packages>
  <Source Name="local" Kind="Directory" Path="Packages" />
  <Source Name="company" Kind="Conan" Url="https://packages.example.test" />

  <LocalPackage Name="NGIN.Base"
                Manifest="Packages/NGIN.Base/NGIN.Base.nginpkg"
                Root="Dependencies/NGIN/NGIN.Base" />

  <Version Name="NGIN.Base" Compatible="0.4" />
  <Version Name="OpenSSL" AtLeast="3.2.0" Before="4.0.0" />

  <Binding Package="OpenSSL" Source="company" Coordinate="openssl" />
</Packages>
```

The semantic model stores the NGIN package coordinate, exact acquired
PackageInstance, provider-native identity/revision/integrity, host or target
context, and provenance separately. This boundary is intentionally suitable
for future Conan, vcpkg, system, registry, or custom providers. Declaring a
provider kind does not imply that the current CLI implements it: unsupported
providers fail explicitly. This release executes only the current Directory
and CMake-oriented provider paths.

A central Version that no discovered project uses is an error. This catches
stale policy instead of letting it look authoritative.

## Policies

Policies are gates, not defaults that projects can override:

```xml
<Policies>
  <PackageProviders IntegrityRequired="true" Locked="true">
    <Allow Kind="Directory" />
    <Allow Kind="Conan" />
  </PackageProviders>

  <Actions Default="Deny" RequireLocked="true" IntegrityRequired="true">
    <Allow Package="NGIN.Reflection"
           Kind="Generate"
           Provider="Directory"
           Reason="First-party reflection generator" />
  </Actions>

  <Paths AllowSymlinks="false" RequireContained="true" />
  <Stage Collision="Error" />

  <Compatibility>
    <Target Name="desktop" />
    <Toolchain Name="msvc" />
  </Compatibility>
</Policies>
```

Lists are repeated typed elements. They are never semicolon-delimited or
encoded into attributes. Conflicting policies, duplicate provider/source
names, ambiguous Target aliases, and references to unknown Targets,
Toolchains, or package Sources are errors.

`Locked="true"` requires integrity and cannot be combined with
`AllowNonHermetic="true"`. Exported Actions remain inactive until a project
selects them, and the trust policy is evaluated before execution.

## Complete conventional workspace

```xml
<Workspace Name="Gallery">
  <Projects>
    <Project Include="Apps/**/*.nginproj" />
    <Project Include="Libraries/**/*.nginproj" />
  </Projects>

  <Configurations>
    <Configuration Name="Debug">
      <Optimization Mode="Off" />
      <DebugSymbols Enabled="true" />
    </Configuration>
    <Configuration Name="Release">
      <Optimization Mode="Speed" />
      <DebugSymbols Enabled="false" />
      <LinkTimeOptimization Enabled="true" />
    </Configuration>
  </Configurations>

  <Targets>
    <Target Name="host" OS="host" Architecture="host" />
  </Targets>

  <Toolchains>
    <Toolchain Name="default" Compiler="default" Linker="default" />
  </Toolchains>

  <Defaults>
    <OutputRoot Path="build/ngin" />
    <Configuration Name="Debug" />
    <Target Name="host" />
    <Toolchain Name="default" />
  </Defaults>

  <Packages>
    <Source Name="local" Kind="Directory" Path="Packages" />
  </Packages>

  <Presets>
    <Preset Name="dev" Command="build">
      <Configuration Name="Debug" />
    </Preset>
  </Presets>
</Workspace>
```

The CLI discovers a workspace through ancestor directories or accepts an exact
path through `--workspace`. `ngin schema --format json` emits structural and
editor metadata generated from `ManifestSpec`; `ngin validate` owns semantic
validation beyond XSD.
