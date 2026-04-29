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

- `Includes`
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

## Shared Model Factoring

Projects may include shared `.nginmodel` files:

```xml
<Includes>
  <Include Path="../Common.nginmodel" />
</Includes>
```

Include paths are relative to the declaring project or model file. Includes are
resolved depth-first in declaration order after the built-in model layer and
after any containing workspace model layer. Missing includes and include cycles
are validation errors.

Project root declarations may also contribute `Defaults`, `Platforms`,
`ProjectTemplates`, and `ProfileTemplates`. These declarations are project-local
model contributions, not build steps or scripts.

`Defaults` may provide scalar values that otherwise repeat across profiles and
build declarations:

```xml
<Defaults BuildType="Debug"
          Platform="linux-x64"
          Environment="local"
          Backend="CMake"
          BuildMode="Generated"
          Language="CXX"
          LanguageStandard="23" />
```

Nearest declaration wins for scalar defaults. Explicit local project/profile
values override defaults.

Built-in project templates:

- `Application` -> `Type="Application"` and executable output
- `Library` -> `Type="Library"` and static-library output
- `Tool` -> `Type="Tool"` and executable output

Authored project templates are declared under `ProjectTemplates`; unknown
template names are validation errors. Explicit project attributes and sections
override template-provided values.

## Inputs

Projects, profiles, environments, profile templates, and packages use typed
blocks under one `Inputs` surface. Authored generic `Input`, `InputSet`,
`Form`, top-level `Sources`, `SourceRoots`, legacy `Inputs/Config`, and
top-level `Contents` are removed from the active contract.

```xml
<Inputs>
  <Sources Path="src" Exclude="platform/**" />
  <Headers Path="include" Visibility="Public" />
  <Configs>
    config/app.cfg
  </Configs>
  <Assets>
    <Directory Path="assets" Include="**/*.png;**/*.wav" />
  </Assets>
  <Contents ContentKind="doc" TargetRoot="share/docs">
    <File Path="docs/readme.txt" />
  </Contents>
  <ToolInputs>
    tools/schema.json
  </ToolInputs>
</Inputs>
```

Supported typed blocks are:

- `Sources`: feeds generated CMake source discovery
- `Headers`: feeds generated CMake header/include discovery
- `Configs`: stages to output and feeds runtime config loading
- `Contents`: stages to output with `ContentKind` or `content`
- `Assets`: stages like content with kind `asset`
- `Generated Role="Source|Content|Asset|ToolInput"`: static declared generated inputs
- `ToolInputs`: validated and emitted as metadata, not compiled or staged by default

`Sources` default to `Visibility="Private"`. `Headers` default to
`Visibility="Public"`. Other typed blocks default to private visibility where
visibility is meaningful. `Required` defaults to `true`.

Typed blocks support three entry shapes:

- block scan: `<Sources Path="src" />`
- text file list: non-empty, non-comment lines inside a typed block
- structured entries: `<File Path="..." />`, `<Directory Path="..." />`, and
  `<Glob Include="..." BasePath="..." />`

Block attributes apply to contained text and structured entries unless an
entry overrides them. `Remove` removes inherited inputs by `Name` or declared
filters before local inputs in the same block are applied. `Override="true"`
on a structured entry replaces an existing effective input with the same
identity.

Inputs may use selectors:

- `Profile`
- `Platform`
- `OperatingSystem`
- `Architecture`
- `BuildType`
- `Environment`
- `Condition`

An input with no selector attributes and no `Condition` applies to all
profiles. An entry with one or more selectors applies only when every
provided selector exactly matches the selected profile.
When a non-selected directory or file is nested under a broader selected
source/header directory, the non-selected path is excluded from generated
source scanning.

Scan semantics are deterministic:

- `<Sources Path="src" />` scans directory `src`.
- `<Sources Path="src" Include="**/*.cpp" Exclude="**/*.generated.cpp" />`
  scans under `src`; patterns are relative to `src`.
- `<Sources Include="src/plugins/**/*.cpp" />` is a rootless glob relative to
  the manifest directory.
- Directory and glob scans are sorted; explicit text and `<File>` entries
  preserve authored order.
- `BasePath` defines the preserved relative path root for glob staging.
- `Target` is valid only on file entries; `TargetRoot` is valid on blocks,
  directories, and globs.

```xml
<Inputs>
  <Sources Path="src" Include="**/*.cpp;**/*.hpp" Exclude="**/*.generated.cpp" />
  <Sources Path="src/linux" OperatingSystem="linux" />
  <Sources>
    <File Path="src/debug_tools.cpp" BuildType="Debug" />
  </Sources>
  <Assets>
    <Glob BasePath="assets" Include="textures/**/*.png" TargetRoot="assets" />
  </Assets>
</Inputs>
```

## Conditions

Projects may declare reusable named conditions under `Conditions`. Conditions
may also come from the built-in catalog, shared model files, and workspace
declarations.

```xml
<Conditions>
  <Condition Name="DesktopDebug">
    <Any>
      <Match OperatingSystem="windows" BuildType="Debug" />
      <Match OperatingSystem="linux" BuildType="Debug" />
      <Match OperatingSystem="macos" BuildType="Debug" />
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

Built-in conditions are `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel`,
`Windows`, `Linux`, `MacOS`, `X64`, `Arm64`, `Desktop`, `Local`,
`Development`, and `Production`. `Local`, `Development`, and `Production`
match environment names `local`, `development`, and `production` exactly.

`Condition` definitions may use selector attributes as shorthand for a single
`Match`. Selectable entries use `Condition` to reference one visible condition
name. Direct selectors and `Condition` may be combined on a selectable item as
implicit AND:

```xml
<Definition Value="MYAPP_DESKTOP_DEBUG"
            Condition="Desktop"
            BuildType="Debug" />
```

Condition names must be unique manifest identifiers. Names that differ only by
case are rejected. Unknown `Condition` and `ConditionRef` names are validation
errors, and condition reference cycles are validation errors. Authored
conditions may not replace built-in condition names.

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

The same selection model applies to project and package references, runtime
module/plugin declarations, runtime enable/disable refs, and environment
feature flags. `Project` references keep `Profile` as the referenced profile
selector; use `Condition`, `Platform`, `OperatingSystem`, `Architecture`,
`BuildType`, and `Environment` to select the reference itself.

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
- `Template` optional
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

`Template` applies a profile template declared by the built-in/workspace/project
model context. Profile resolution order is:

1. inherited profile via `Extends`
2. profile template contributions
3. local profile attributes and children

Profile templates may provide scalar profile attributes, `Launch`, `References`,
`Inputs`, and `Runtime`. Unknown profile templates and profile template cycles
are validation errors.

Supported profile child sections:

- `References`
- `Inputs`
- `Launch`
- `Runtime`

`<Launch>` is launch-only metadata. Library projects may not declare it.
Launchable projects may declare a root `<Launch>` directly under `<Project>`;
that launch metadata applies to profiles that do not declare their own
profile-level `<Launch>`. `$(OutputName)` in launch executable metadata resolves
to the selected project output name.

## Environments

Projects may define named environment layers under `Environments`.

Each environment may contribute:

- `References`
- `Inputs`
- `Variables`
- `Features`
- `Runtime`

`Features` may contain environment feature flags and package feature selections.
Package feature selections use explicit opt-in:

```xml
<Features>
  <Use Package="NGIN.Core" Feature="Reflection" />
  <Disable Package="NGIN.Diagnostics" Feature="Diagnostics" />
</Features>
```

Project-level and profile-level `Features` support the same `Use` and `Disable`
entries. `Use` may declare `Version` or `VersionRange`; otherwise workspace or
project dependency policy may supply the range. Phase C selectors and
`Condition` are valid on `Use` and `Disable`.

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
         Template="Application"
         DefaultProfile="Runtime">
  <Inputs>
    <Sources Path="src" />
  </Inputs>
  <Launch Executable="$(OutputName)" WorkingDirectory="." />
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
             Environment="development" />
  </Profiles>
</Project>
```
