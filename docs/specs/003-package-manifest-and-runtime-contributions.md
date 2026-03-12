# Spec 003: Package Manifest and Runtime Contributions

Status: Active
Last updated: 2026-03-12

## Purpose

This spec defines the `.nginpkg` file format and the rule that packages own both runtime contributions and the umbrella-facing integration contract.

Packages are the primary reusable unit in NGIN.

This spec defines the authored package manifest only. It does not define distributable package archives or installed package stores. Those are covered by Spec 009.

## File Contract

Filename:

- `<PackageName>.nginpkg`

Root element:

- `<Package>`

Required root attributes:

- `SchemaVersion`
- `Name`
- `Version`

Optional root attributes:

- `CompatiblePlatformRange`

Recommended top-level child sections:

- `SourceBinding`
- `Artifacts`
- `Build`

## Structure

```xml
<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="1"
         Name="NGIN.Core"
         Version="0.1.0"
         CompatiblePlatformRange=">=0.1.0 &lt;1.0.0">
  <SourceBinding Kind="Source" Path="Packages/NGIN.Core" />
  <Artifacts>
    <Libraries>
      <Library Name="NGIN.Core" Target="NGIN::Core" Linkage="Static" />
    </Libraries>
  </Artifacts>
  <Build Backend="CMake" />
  <Platforms>
    <Platform Name="linux" />
    <Platform Name="windows" />
    <Platform Name="macos" />
  </Platforms>
  <Dependencies>
    <Dependency Name="NGIN.Base" VersionRange=">=0.1.0 &lt;1.0.0" Optional="false" />
    <Dependency Name="NGIN.Log" VersionRange=">=0.1.0 &lt;1.0.0" Optional="false" />
  </Dependencies>
  <Modules>
    <Module Name="Core.Hosting"
            Family="Core"
            Type="Runtime"
            StartupStage="Services"
            Version="0.1.0"
            CompatiblePlatformRange=">=0.1.0 &lt;1.0.0"
            ReflectionRequired="false">
      <Platforms>
        <Platform Name="linux" />
        <Platform Name="windows" />
        <Platform Name="macos" />
      </Platforms>
      <Dependencies>
        <Dependency Name="Base.Foundation" RequiredVersion=">=0.1.0 &lt;1.0.0" Optional="false" />
        <Dependency Name="Log.Foundation" RequiredVersion=">=0.1.0 &lt;1.0.0" Optional="false" />
      </Dependencies>
      <SupportedHosts />
      <ProvidesServices />
      <RequiresServices />
      <Capabilities />
    </Module>
  </Modules>
  <Plugins />
  <Contents />
</Package>
```

## Dependencies

Package dependencies are declared in `<Dependencies>`.

Each `<Dependency>` may define:

- `Name` required
- `VersionRange` optional but recommended
- `Optional` optional, defaults to `false`

## Runtime Contributions

Packages own runtime contributions.

Supported sections:

- `Modules`
- `Plugins`
- `Contents`
- `Bootstrap`

## Source Binding, Artifacts, and Build Integration

Packages are also the authoritative integration layer for the umbrella workspace.

### SourceBinding

`<SourceBinding>` identifies what backs the package in the public package model.

Supported `Kind` values:

- `Source`
- `CMakePackage`
- `Prebuilt`

`Path` is workspace-relative when the package is backed by local source or local package content.

### Artifacts

`<Artifacts>` describes what a package exposes to builds and staged targets.

Start with only:

- `Libraries`
- `Executables`

Each `<Library>` may define:

- `Name` required
- `Target` optional but recommended
- `Linkage` optional
- `Origin` optional
- `Exported` optional, defaults to `true`

`Linkage` values:

- `Static`
- `Shared`
- `Interface`

`Origin` values:

- `Built`
- `Imported`
- `Prebuilt`

If `Origin` is omitted, implementations may infer it from `SourceBinding`:

- `Source` usually implies `Built`
- `CMakePackage` usually implies `Imported`
- `Prebuilt` usually implies `Prebuilt`

Each `<Executable>` may define:

- `Name` required
- `Target` optional but recommended
- `Origin` optional
- `Exported` optional, defaults to `true`

### Build

`<Build>` describes backend mapping details that cannot be inferred from `Artifacts`.

Current active backend:

- `CMake`

`Build` should remain optional and backend-thin.

### Modules

Modules declared inside a package define runtime composition metadata.

Each `<Module>` may define:

- `Name` required
- `Family` optional, defaults to `Core`
- `Type` optional, defaults to `Runtime`
- `StartupStage` optional, defaults to `Features`
- `Version` optional but recommended
- `CompatiblePlatformRange` optional
- `ReflectionRequired` optional, defaults to `false`

Supported module child sections:

- `Platforms`
- `Dependencies`
- `SupportedHosts`
- `ProvidesServices`
- `RequiresServices`
- `Capabilities`

`StartupStage` is a coarse startup-order hint, not an architectural layer or host selector.
Dependencies remain the authoritative ordering rule; `StartupStage` provides broad grouping and deterministic tie-breaking.

Supported `StartupStage` values:

- `Foundation`
- `Platform`
- `Services`
- `Features`
- `Presentation`

`SupportedHosts` restricts which host profiles may activate the module. If the section is omitted or empty, the module is valid for all hosts.

Supported `<Host Name="...">` values:

- `ConsoleApp`
- `GuiApp`
- `Game`
- `Editor`
- `Service`
- `TestHost`

### Plugins

Plugins declared inside a package define optional runtime extensions provided by that package.

The exact binary ABI and distribution model remain future work. The package is still the primary public container.

### Contents

Each `<File>` may define:

- `Source` required
- `Target` required
- `Kind` optional but recommended

Content paths are relative to the package file unless absolute.

## Rules

- packages are the primary reusable unit referenced by projects
- runtime module and plugin declarations belong inside packages
- package wrappers are the authoritative NGIN-facing integration contract in the umbrella workspace
- `SourceBinding` describes what backs the package, while `Artifacts` describes what the package exposes
- `Build` should stay limited to backend-mapping details, not become a second overloaded package contract
- standalone runtime descriptor files are not part of the intended primary authoring model
- package names and versions identify reusable units
- content staging paths must be explicit and conflict-checked during composition

## Implementation Note

Current runtime code may still load lower-level dynamic descriptor files internally. That is an implementation seam, not the public authored model defined by this spec.
