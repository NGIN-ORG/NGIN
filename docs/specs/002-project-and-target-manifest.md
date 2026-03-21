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
- `Host`
- `References`
- `ConfigSources`
- `Runtime`
- `Configurations`

`Runtime` is optional advanced metadata. It is not required for normal application authoring.

## Output

Projects own one `Output` definition:

```xml
<Output Kind="Executable|Library"
        Name="MyApp"
        Target="MyApp"
        FileName="MyApp" />
```

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

## Host

Projects may define a root host default:

```xml
<Host Profile="ConsoleApp|GuiApp|Game|Editor|Service|TestHost" />
```

## Configurations

Projects declare configurations under `Configurations`.

```xml
<Configurations>
  <Configuration Name="Runtime"
                 BuildConfiguration="Debug"
                 HostProfile="Game"
                 Platform="Linux"
                 Environment="Dev"
                 WorkingDirectory=".">
    <Launch Executable="MyApp" />
  </Configuration>
</Configurations>
```

Supported configuration attributes:

- `Name` required
- `BuildConfiguration` optional
- `Platform` optional
- `Environment` optional
- `WorkingDirectory` optional
- `HostProfile` optional
- `EnableReflection` optional

Supported configuration child sections:

- `References`
- `ConfigSources`
- `Launch`
- `EnableModules`
- `DisableModules`
- `EnablePlugins`
- `DisablePlugins`

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
  <Configurations>
    <Configuration Name="Runtime"
                   BuildConfiguration="Debug"
                   HostProfile="ConsoleApp"
                   Environment="Dev"
                   WorkingDirectory=".">
      <Launch Executable="App.Basic" />
    </Configuration>
  </Configurations>
</Project>
```
