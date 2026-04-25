# Spec 002: Project Manifest

Status: Active
Last updated: 2026-03-21

## Purpose

This spec defines the active V2 `.nginproj` contract.

## File Contract

- filename: `<ProjectName>.nginproj`
- root element: `<Project>`
- required root attributes:
  - `SchemaVersion="2"`
  - `Name`
  - `Type`
  - `DefaultConfiguration`

## Root Surface

Supported root sections:

- `SourceRoots`
- `Output`
- `Build`
- `References`
- `ConfigSources`
- `Runtime`
- `Environments`
- `Configurations`

`Runtime` is optional advanced metadata. It is not required for normal application authoring.

## Output

Projects own one `Output` definition. `Type` is the authored project class and `Output Kind` is the artifact form.

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
</Build>
```

When enabled, `ngin build` runs MetaGen before generated CMake emission and
compiles the generated reflection source as part of the project target. Reflected
types must be declared in includable headers, not compiled source files.

## References

Projects use one unified `References` surface:

```xml
<References>
  <Project Path="../Game.Engine/Game.Engine.nginproj" Configuration="Runtime" />
  <Package Name="NGIN.Core" Version="0.1.0" Optional="false" />
</References>
```

Reference resolution rules:

- explicit referenced project `Configuration` wins
- otherwise try the selected root project configuration name in the referenced project
- otherwise use the referenced project `DefaultConfiguration`

## Configurations

Projects declare configurations under `Configurations`.

```xml
<Configurations>
  <Configuration Name="Runtime"
                 BuildConfiguration="Debug"
                 OperatingSystem="linux"
                 Architecture="x64"
                 Environment="development">
    <Launch Executable="MyApp" WorkingDirectory="." />
  </Configuration>
</Configurations>
```

Supported configuration attributes:

- `Name` required
- `BuildConfiguration` optional
- `OperatingSystem` optional
- `Architecture` optional
- `Environment` required
- `EnableReflection` optional

Supported configuration child sections:

- `References`
- `ConfigSources`
- `Launch`
- `Runtime`

`<Launch>` is launch-only metadata. Library projects may not declare it.

## Environments

Projects may define named environment layers under `Environments`.

Each environment may contribute:

- `References`
- `ConfigSources`
- `Variables`
- `Features`
- `Contents`
- `Runtime`

## Example

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2"
         Name="App.Basic"
         Type="Application"
         DefaultConfiguration="Runtime">
  <SourceRoots>
    <SourceRoot Path="src" />
  </SourceRoots>
  <Output Kind="Executable" Name="App.Basic" Target="App.Basic" />
  <References>
    <Package Name="NGIN.Core" Version="0.1.0" />
  </References>
  <Environments>
    <Environment Name="development" />
  </Environments>
  <Configurations>
    <Configuration Name="Runtime"
                   BuildConfiguration="Debug"
                   OperatingSystem="linux"
                   Architecture="x64"
                   Environment="development">
      <Launch Executable="App.Basic" WorkingDirectory="." />
    </Configuration>
  </Configurations>
</Project>
```
