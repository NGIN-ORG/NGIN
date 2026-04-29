# `.nginproj` Authoring

This guide is the short, practical version of the NGIN project manifest model.
It is for developers who want to author or review project files without reading
the full spec first.

An `.nginproj` describes one C++ project. CMake still performs the backend
build. NGIN owns the higher-level project shape: sources, output artifact,
dependencies, profiles, and local run metadata.

## Minimal Project

```xml
<Project SchemaVersion="3"
         Name="MyTool"
         Template="Tool"
         DefaultProfile="Runtime">
  <Defaults BuildType="Debug" Platform="linux-x64" Environment="local" />

  <Inputs>
    <Sources Path="src" />
  </Inputs>

  <Launch Executable="$(OutputName)" WorkingDirectory="." />

  <Environments>
    <Environment Name="local" />
  </Environments>

  <Profiles>
    <Profile Name="Runtime" />
  </Profiles>
</Project>
```

## Main Sections

| Section | Purpose |
| --- | --- |
| `Includes` | Shared `.nginmodel` files |
| `Defaults` | Local scalar defaults for build/profile values |
| `Inputs` | Source, config, content, asset, generated, and tool inputs |
| `Output` | Optional artifact type, name, and backend target |
| `Build` | Backend mode, language, standard, and build settings |
| `References` | Project and package dependencies |
| `Environments` | Named environment layers |
| `Profiles` | Selectable project profiles |

Profiles can reuse an earlier profile with `Extends`:

```xml
<Profiles>
  <Profile Name="Runtime"
           BuildType="Debug"
           Platform="linux-x64"
           Environment="local" />
  <Profile Name="Shipping"
           Extends="Runtime"
           BuildType="Release" />
</Profiles>
```

Shared model files reduce repeated defaults across projects:

```xml
<Model SchemaVersion="3" Name="Common">
  <Defaults BuildType="Debug" Platform="linux-x64" />
  <ProfileTemplates>
    <ProfileTemplate Name="LocalRuntime" Environment="local">
      <Launch Executable="$(OutputName)" WorkingDirectory="." />
    </ProfileTemplate>
  </ProfileTemplates>
</Model>
```

Projects include them with paths relative to the declaring file:

```xml
<Includes>
  <Include Path="../Common.nginmodel" />
</Includes>
```

## Inputs

Use `Inputs` to describe what belongs to the target and what should be staged
or exposed to tools.

```xml
<Inputs>
  <Headers Path="include" Visibility="Public" />
  <Sources Path="src" />
  <Configs>
    config/app.cfg
  </Configs>
</Inputs>
```

`Headers` default to public visibility. `Sources` default to private
visibility. Visibility can still be written explicitly when that is clearer.

For manually curated source lists:

```xml
<Inputs>
  <Sources>
    src/main.cpp
    src/app.cpp
    src/window.cpp
  </Sources>
</Inputs>
```

For root scanning with glob filters:

```xml
<Inputs>
  <Sources Path="src"
           Include="**/*.cpp;**/*.hpp"
           Exclude="**/*.generated.cpp" />
</Inputs>
```

Patterns are relative to the root and support `*`, `?`, and `**`.
Rootless globs are relative to the manifest directory. For staged globs,
`BasePath` defines which path segment is preserved under `TargetRoot`.

## Selection

Inputs and build settings can be selected by project profile
values. Simple local selection uses typed selector attributes.

```xml
<Inputs>
  <Sources Path="src" Exclude="platform/**" />
  <Sources Path="src/platform/windows" OperatingSystem="windows" />
  <Sources Path="src/platform/linux" OperatingSystem="linux" />
  <Sources BuildType="Debug">
    src/debug_overlay.cpp
    src/debug_trace.cpp
  </Sources>
</Inputs>
```

An item with no selector applies to every profile. An item with selectors
applies only when all selectors match the active profile. If a
non-selected typed path is nested under a broader selected root, NGIN excludes
that nested path from source scanning.

Supported selectors:

- `Profile`
- `OperatingSystem`
- `Architecture`
- `BuildType`
- `Environment`

The same selectors can be used on build settings:

```xml
<Build Backend="CMake" Mode="Generated" Language="CXX" LanguageStandard="23">
  <CompileDefinitions>
    <Definition Value="MYTOOL_DEBUG"
                Visibility="Private"
                BuildType="Debug" />
  </CompileDefinitions>
</Build>
```

Reusable or non-trivial selection can use named conditions:

```xml
<Conditions>
  <Condition Name="Desktop">
    <Any>
      <Match OperatingSystem="windows" />
      <Match OperatingSystem="linux" />
      <Match OperatingSystem="macos" />
    </Any>
  </Condition>
</Conditions>
```

Use `Condition` to reference a named condition. `Condition` and direct selectors can be
combined and are evaluated as AND:

```xml
<Build Backend="CMake" Mode="Generated" Language="CXX" LanguageStandard="23">
  <CompileDefinitions Condition="Desktop">
    <Definition Value="MYTOOL_DESKTOP" Visibility="Private" />
    <Definition Value="MYTOOL_DESKTOP_DEBUG"
                Visibility="Private"
                BuildType="Debug" />
  </CompileDefinitions>
</Build>
```

## Output

The project owns one output artifact.

```xml
<Output Kind="Executable" Name="MyTool" Target="MyTool" />
```

Valid project/output pairings:

- `Application` -> `Executable`
- `Tool` -> `Executable`
- `Library` -> `StaticLibrary` or `SharedLibrary`

When `Output` is omitted, `Template="Application"` and `Template="Tool"` infer an
executable output using the project name. `Template="Library"` infers a static
library output. Explicit `Output` remains useful when the backend target name
should differ from the project name.

## References

Project references:

```xml
<References>
  <Project Path="../Engine/Engine.nginproj" Profile="Runtime" />
</References>
```

Package references:

```xml
<References>
  <Package Name="NGIN.Core" Version="0.1.0" />
</References>
```

## Profiles

NGIN separates the project profile name from the backend build
profile.

```xml
<Profile Name="Runtime"
               BuildType="Debug"
               Platform="linux-x64"
               Environment="local" />
```

`Name` selects the authored project profile. `BuildType` maps to
the backend build type such as `Debug` or `Release`.

Executable projects can add launch metadata:

```xml
<Launch Executable="$(OutputName)" WorkingDirectory="." />
```

A root project `Launch` applies to launchable profiles that do not declare their
own profile launch. Library projects may not declare launch metadata.

## Useful Commands

```bash
ngin validate --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj --profile Runtime
ngin graph --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj --profile Runtime
ngin build --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj --profile Runtime
ngin run --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj --profile Runtime
```
