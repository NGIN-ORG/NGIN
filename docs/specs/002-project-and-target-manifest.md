# Spec 002: Project Manifest

Status: Active
Last updated: 2026-04-28

## Purpose

This spec defines the active V3 `.nginproj` contract.

## File Contract

- filename: `<ProjectName>.nginproj`
- root element: `<Project>`
- required root attributes:
  - `SchemaVersion="3"`
  - `Name`
- optional root attributes:
  - `Template`
  - `Type`
  - `DefaultProfile`

## Root Surface

Supported root sections:

- `Sources`
- `Conditions`
- `Defaults`
- `Platforms`
- `Output`
- `Build`
- `References`
- `Inputs`
- `LocalSettings`
- `Runtime`
- `Environments`
- `Profiles`

`Runtime` is optional advanced metadata. It is not required for normal application authoring.

`Sources` is the source declaration surface for generated-mode projects.

## Sources

Projects may declare public and private source ownership under `Sources`.

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

Supported source groups:

- `Public`
- `Private`

Supported source entries:

- `Root Path="..."`
- `File Path="..."`
- `Files` line-separated file list

Generated CMake maps selected public roots to `PUBLIC` include directories and
selected private roots to `PRIVATE` include directories. Source files discovered
under selected roots and selected explicit files are deduplicated by resolved
path before target emission.

Source roots and files may use typed selectors:

- `Profile`
- `Platform`
- `OperatingSystem`
- `Architecture`
- `BuildType`
- `Environment`

An entry with no selector attributes and no `Condition` applies to all
profiles. An entry with one or more selectors applies only when every
provided selector exactly matches the selected profile.
When a non-selected typed root or file is nested under a broader selected root,
the non-selected path is excluded from generated source scanning.

Source entries may also use `Condition` to reference a named condition declared in
the project-local `Conditions` section. Direct selectors and `Condition` may be
combined on the same entry and are evaluated as implicit AND.

`Root` may constrain recursive discovery with `Include` and `Exclude` glob
patterns. Patterns are relative to the root path, use `/` separators, and
support `*`, `?`, and `**`. Multiple patterns may be separated by semicolons,
commas, or line breaks. Without `Include`, roots discover compilable source
files under the root. With `Include`, roots discover supported source/header
files matching at least one include pattern, then remove files matching
`Exclude`.

```xml
<Sources>
  <Private>
    <Root Path="src"
          Include="**/*.cpp;**/*.hpp"
          Exclude="**/*.generated.cpp" />
    <Root Path="src/linux" OperatingSystem="linux" />
    <File Path="src/debug_tools.cpp" BuildType="Debug" />
    <Files BuildType="Debug">
      src/debug_overlay.cpp
      src/debug_trace.cpp
    </Files>
  </Private>
</Sources>
```

## Conditions

Projects may declare reusable named conditions under `Conditions`.

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

Supported condition nodes:

- `Match`
- `All`
- `Any`
- `Not`
- `ConditionRef`

`Condition` definitions may use selector attributes as shorthand for a single `Match`.
Selectable entries use `Condition` to reference one project-local condition name. Direct selectors and `Condition`
may be combined on a selectable item as implicit AND:

```xml
<Definition Value="MYAPP_DESKTOP_DEBUG"
            Condition="Desktop"
            BuildType="Debug" />
```

Condition names must be unique manifest identifiers. Names that differ only by
case are rejected. Unknown `Condition` and `ConditionRef` names are validation
errors, and condition reference cycles are validation errors.

## Output

Projects may declare one `Output` definition. When omitted, NGIN infers the
output from the project name and project template/type. `Type` is the normalized
project class and `Output Kind` is the artifact form.

```xml
<Output Kind="Executable|StaticLibrary|SharedLibrary"
        Name="MyApp"
        Target="MyApp"
        FileName="MyApp" />
```

Valid pairings:

- `Application` -> `Executable`
- `Tool` -> `Executable`
- `Library` -> `StaticLibrary|SharedLibrary`

## Build

Generated CMake projects may opt into MetaGen under `Build`.

```xml
<Build Backend="CMake" Mode="Generated" Language="CXX" LanguageStandard="23">
  <MetaGen Enabled="true" />
  <CompileDefinitions>
    <Definition Value="MYAPP_DEBUG_TOOLS"
                Visibility="Private"
                BuildType="Debug" />
  </CompileDefinitions>
</Build>
```

When enabled, `ngin build` runs MetaGen before generated CMake emission and
compiles the generated reflection source as part of the project target. Reflected
types must be declared in includable headers, not compiled source files.

MetaGen property methods use `NGIN_PROPERTY(...)` on public member functions.
A getter has zero parameters and a non-void return. A setter has one parameter
and returns `void`. Getter and setter methods are paired by their reflected
`name` option:

```cpp
NGIN_PROPERTY(name = "score")
int GetScore() const;

NGIN_PROPERTY(name = "score")
void SetScore(int value);
```

Getter-only properties are supported. If a getter returns a mutable lvalue
reference, the reflection runtime can write through that reference. Setter-only
properties and invalid property method signatures are rejected by MetaGen.

Build settings under `IncludeDirectories`, `CompileDefinitions`,
`CompileOptions`, and `LinkOptions` may use the same typed selector attributes
and `Condition` references as `Sources` entries. Selected settings are emitted only
when their effective condition matches the active project profile. Build
setting groups may also use selectors and `Condition`; group selection is inherited
by child entries as implicit AND.

## References

Projects use one unified `References` surface:

```xml
<References>
  <Project Path="../Game.Engine/Game.Engine.nginproj" Profile="Runtime" />
  <Package Name="NGIN.Core" Version="0.1.0" Optional="false" />
</References>
```

Reference resolution rules:

- explicit referenced project `Profile` wins
- otherwise try the selected root project profile name in the referenced project
- otherwise use the referenced project `DefaultProfile`

## Profiles

Projects declare profiles under `Profiles`.

```xml
<Profiles>
  <Profile Name="Runtime"
           BuildType="Debug"
           Platform="linux-x64"
           Environment="development">
    <Launch Executable="MyApp" WorkingDirectory="." />
  </Profile>
  <Profile Name="Shipping"
           Extends="Runtime"
           BuildType="Release" />
</Profiles>
```

Supported profile attributes:

- `Name` required
- `Extends` optional
- `BuildType` optional
- `Platform` optional
- `OperatingSystem` optional
- `Architecture` optional
- `Environment` required
- `EnableReflection` optional

`Extends` copies an earlier profile's resolved scalar settings and authored
profile contributions. The extending profile may override scalar attributes and
append child-section contributions. Forward references and cycles are rejected
by requiring the base profile to appear first.

Supported profile child sections:

- `References`
- `Inputs`
- `Launch`
- `Runtime`

`<Launch>` is launch-only metadata. Library projects may not declare it.

## Environments

Projects may define named environment layers under `Environments`.

Each environment may contribute:

- `References`
- `Inputs`
- `Variables`
- `Features`
- `Contents`
- `Runtime`

### Variables

Environment variables use exactly one value source:

- `Value`
- `FromEnvironment`
- `FromLocalSetting`

`Value` is a literal committed manifest value. `FromEnvironment` reads one
operating system environment variable. `FromLocalSetting` reads one key from
loaded local settings.

Supported variable attributes:

- `Name` required
- `Value` optional source selector
- `FromEnvironment` optional source selector
- `FromLocalSetting` optional source selector
- `Required` optional
- `Secret` optional

`Secret="true"` may not be combined with a literal `Value` in a committed
project manifest. Secret variables must use `FromEnvironment` or
`FromLocalSetting`.

## Local Settings

Projects may declare imported repository-local settings under a root
`LocalSettings` section:

```xml
<LocalSettings>
  <Import Path=".ngin/local/user.nginsettings" Optional="true" />
</LocalSettings>
```

Supported import attributes:

- `Path` required
- `Optional` optional

Imported paths are resolved relative to the project manifest unless the path is
absolute. User-global settings at `~/.ngin/settings.nginsettings` are also
available as an inert fallback source for variables that explicitly use
`FromLocalSetting`.

Local settings files use the `.nginsettings` extension:

```xml
<?xml version="1.0" encoding="utf-8"?>
<LocalSettings SchemaVersion="1">
  <Settings>
    <Setting Key="feeds.private.token" Value="..." Secret="true" />
    <Setting Key="sdk.vulkan.root" Value="/opt/vulkan-sdk" />
  </Settings>
</LocalSettings>
```

Supported local settings attributes:

- root `SchemaVersion="1"` required
- `Setting Key` required
- `Setting Value` required
- `Setting Secret` optional

Project variable names and local setting keys are separate namespaces.

## Example

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3"
         Name="App.Basic"
         DefaultProfile="Runtime">
  <Sources>
    <Private>
      <Root Path="src" />
    </Private>
  </Sources>
  <Output Kind="Executable" Name="App.Basic" Target="App.Basic" />
  <References>
    <Package Name="NGIN.Core" Version="0.1.0" />
  </References>
  <Environments>
    <Environment Name="development" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime"
                   BuildType="Debug"
                   Platform="linux-x64"
                   Environment="development">
      <Launch Executable="App.Basic" WorkingDirectory="." />
    </Profile>
  </Profiles>
</Project>
```
