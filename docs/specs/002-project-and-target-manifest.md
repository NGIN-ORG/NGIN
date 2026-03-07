# Spec 002: Project and Target Manifest

Status: Active
Last updated: 2026-03-07

## Purpose

This spec defines the `.nginproj` file format.

A project manifest is the top-level authored application definition.

## File Contract

Filename:

- `<ProjectName>.nginproj`

Root element:

- `<Project>`

Required root attributes:

- `SchemaVersion`
- `Name`
- `DefaultTarget`

## Structure

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="1" Name="Sandbox.Game" DefaultTarget="Game">
  <Targets>
    <Target Name="Game"
            Type="Runtime"
            Profile="Game"
            Platform="linux-x64"
            EnableReflection="false"
            Environment="Dev"
            WorkingDirectory=".">
      <Packages>
        <PackageRef Name="NGIN.Core" VersionRange=">=0.1.0 &lt;1.0.0" />
        <PackageRef Name="NGIN.ECS" VersionRange=">=0.1.0 &lt;1.0.0" />
      </Packages>
      <Modules>
        <Enable Name="Core.Hosting" />
      </Modules>
      <Plugins />
      <ConfigSources>
        <Config Source="config/game.xml" />
      </ConfigSources>
    </Target>
  </Targets>
</Project>
```

## Target Shape

Each `<Target>` may define:

- `Name` required
- `Type` required
- `Profile` optional but recommended
- `Platform` required
- `EnableReflection` optional, defaults to `false`
- `Environment` optional
- `WorkingDirectory` optional, defaults to `.`

Supported child sections:

- `Packages`
- `Modules`
- `Plugins`
- `ConfigSources`

### Packages

Packages are the normal composition path.

Each `<PackageRef>` may define:

- `Name` required
- `VersionRange` optional but recommended
- `Optional` optional, defaults to `false`

### Modules

`Modules` is an advanced override section.

Supported elements:

- `<Enable Name="..." />`
- `<Disable Name="..." />`

Direct module toggles should be rare. Packages remain the primary composition input.

### Plugins

`Plugins` is an advanced override section.

Supported elements:

- `<Enable Name="..." />`
- `<Disable Name="..." />`

Direct plugin toggles should be rare. Package-declared plugin contributions remain primary.

### ConfigSources

Each `<Config>` entry defines a relative or absolute source path loaded into the target configuration model.

## Rules

- one project file defines one application
- target names must be unique inside a project
- `DefaultTarget` must name an existing target
- target package references are the normal path for composition
- module/plugin sections are advanced overrides, not the main authoring surface
- relative `ConfigSources` and `WorkingDirectory` values are resolved relative to the project file location

## Output

Selecting a target from a project produces the input to composition and validation.
