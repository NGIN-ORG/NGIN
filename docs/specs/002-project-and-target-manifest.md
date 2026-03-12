# Spec 002: Project And Variant Manifest

Status: Active
Last updated: 2026-03-12

## Purpose

This spec defines the active `.nginproj` file format.

A project manifest is the top-level authored buildable unit in NGIN.

Projects own:

- source roots
- primary output
- project references
- package references
- app-local runtime contributions
- variants

## File Contract

Filename:

- `<ProjectName>.nginproj`

Root element:

- `<Project>`

Required root attributes:

- `SchemaVersion`
- `Name`
- `Type`
- `DefaultVariant`

## Structure

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="1"
         Name="Sandbox.Game"
         Type="Application"
         DefaultVariant="Game">
  <SourceRoots>
    <SourceRoot Path="src" />
  </SourceRoots>
  <PrimaryOutput Kind="Executable"
                 Name="Sandbox.Game"
                 Target="Sandbox.Game" />
  <PackageRefs>
    <PackageRef Name="NGIN.Core" VersionRange=">=0.1.0 &lt;1.0.0" />
    <PackageRef Name="NGIN.ECS" VersionRange=">=0.1.0 &lt;1.0.0" />
  </PackageRefs>
  <ConfigSources>
    <Config Source="config/game.xml" />
  </ConfigSources>
  <Runtime>
    <Modules>
      <Module Name="App.GameRuntime"
              Family="App"
              Type="Runtime"
              StartupStage="Features"
              Version="0.1.0"
              ReflectionRequired="false" />
    </Modules>
    <EnableModules>
      <ModuleRef Name="App.GameRuntime" />
    </EnableModules>
  </Runtime>
  <Variants>
    <Variant Name="Game"
             Profile="Game"
             Platform="linux-x64"
             Environment="Dev"
             WorkingDirectory=".">
      <Launch Executable="Sandbox.Game" />
    </Variant>
  </Variants>
</Project>
```

## Project Shape

Root-level project sections:

- `SourceRoots`
- `PrimaryOutput`
- `ProjectRefs`
- `PackageRefs`
- `ConfigSources`
- `Runtime`
- `Variants`

### SourceRoots

`SourceRoots` declares project-owned source directories.

Each `<SourceRoot>` entry defines:

- `Path` required

### PrimaryOutput

`PrimaryOutput` declares the main artifact produced by the project.

Required attributes:

- `Kind`
- `Name`
- `Target`

Supported `Kind` values in v1:

- `Executable`
- `StaticLibrary`
- `SharedLibrary`

Projects may define additional outputs later, but v1 requires one primary output.

### ProjectRefs

`ProjectRefs` declares source/build graph dependencies on other workspace projects.

Each `<ProjectRef>` may define:

- `Path` required
- `Variant` optional

`ProjectRef` is distinct from `PackageRef`. Project references are workspace-local build graph edges, not reusable package dependencies.

### PackageRefs

Packages are the normal reusable dependency path.

Each `<PackageRef>` may define:

- `Name` required
- `VersionRange` optional but recommended
- `Optional` optional, defaults to `false`

### ConfigSources

Each `<Config>` entry defines a relative or absolute source path loaded into the project configuration model.

### Runtime

`Runtime` declares app-local runtime contributions owned by the project itself.

Supported child sections in v1:

- `Modules`
- `EnableModules`
- `DisableModules`

This allows applications to own their entry modules without forcing those modules through a reusable package.

Project-owned runtime modules use the same module descriptor fields as package-owned modules, including `StartupStage` and optional `SupportedHosts`.

### Variants

Variants define build/run variants of the same project.

Each `<Variant>` may define:

- `Name` required
- `Profile` required
- `Platform` required
- `Environment` optional
- `WorkingDirectory` optional, defaults to `.`
- `EnableReflection` optional, defaults to `false`

Supported child sections:

- `PackageRefs`
- `ConfigSources`
- `Launch`
- `EnableModules`
- `DisableModules`

### Launch

`Launch` is optional.

It exists only to resolve ambiguity when a variant can see more than one executable artifact through its project output plus package graph.

Supported attributes:

- `Executable` optional

Rules:

- if a project resolves exactly one executable artifact, implementations may infer it
- if a project resolves multiple executable artifacts, `Launch Executable="..."` should be used to choose the runnable output
- if a project resolves no executable artifacts, the variant may still be valid for validation or staging, but it has no inferred runnable output

## Rules

- one project file defines one authored buildable unit
- variant names must be unique inside a project
- `DefaultVariant` must name an existing variant
- root-level package references apply to the whole project
- variants may add package/config/module overrides
- projects may define app-local runtime modules
- package references remain the normal path for reusable composition
- `ProjectRef` and `PackageRef` are different dependency types and must not be conflated
- relative `ConfigSources` and `WorkingDirectory` values are resolved relative to the project file location

## Output

Selecting a variant from a project produces the input to composition, validation, build planning, and staging.
