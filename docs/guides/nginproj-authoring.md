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
  <Sources>
    <Private>
      <Root Path="src" />
    </Private>
  </Sources>

  <Output Kind="Executable" Name="MyTool" Target="MyTool" />

  <Build Backend="CMake"
         Mode="Generated"
         Language="CXX"
         LanguageStandard="23" />

  <Environments>
    <Environment Name="local" />
  </Environments>

  <Profiles>
    <Profile Name="Runtime"
             BuildType="Debug"
             Platform="linux-x64"
             Environment="local">
      <Launch Executable="MyTool" WorkingDirectory="." />
    </Profile>
  </Profiles>
</Project>
```

## Main Sections

| Section | Purpose |
| --- | --- |
| `Sources` | Public and private source ownership |
| `Output` | Artifact type, name, and backend target |
| `Build` | Backend mode, language, standard, and build settings |
| `References` | Project and package dependencies |
| `Inputs` | Runtime config files contributed by the project |
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

## Sources

Use `Sources` to describe what belongs to the target.

```xml
<Sources>
  <Public>
    <Root Path="include" />
  </Public>

  <Private>
    <Root Path="src" />
  </Private>
</Sources>
```

`Public` roots become public include directories for library targets. `Private`
roots become private include directories and implementation source roots.

For manually curated source lists:

```xml
<Sources>
  <Private>
    <Files>
      src/main.cpp
      src/app.cpp
      src/window.cpp
    </Files>
  </Private>
</Sources>
```

`<Files>` is equivalent to repeated `<File Path="..." />` entries.

For root scanning with glob filters:

```xml
<Sources>
  <Private>
    <Root Path="src"
          Include="**/*.cpp;**/*.hpp"
          Exclude="**/*.generated.cpp" />
  </Private>
</Sources>
```

Patterns are relative to the root and support `*`, `?`, and `**`.

## Selection

Source entries and build settings can be selected by project profile
values. Simple local selection uses typed selector attributes.

```xml
<Sources>
  <Private>
    <Root Path="src" />
    <Root Path="src/platform/windows" OperatingSystem="windows" />
    <Root Path="src/platform/linux" OperatingSystem="linux" />

    <Files BuildType="Debug">
      src/debug_overlay.cpp
      src/debug_trace.cpp
    </Files>
  </Private>
</Sources>
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
<Launch Executable="MyTool" WorkingDirectory="." />
```

## Useful Commands

```bash
ngin validate --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj --profile Runtime
ngin graph --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj --profile Runtime
ngin build --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj --profile Runtime
ngin run --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj --profile Runtime
```
