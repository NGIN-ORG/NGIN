# Spec 003: Package Manifest and Runtime Contributions

Status: Active
Last updated: 2026-03-07

## Purpose

This spec defines the `.nginpkg` file format and the rule that packages own runtime contributions.

Packages are the primary reusable unit in NGIN.

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

## Structure

```xml
<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="1"
         Name="NGIN.Editor"
         Version="0.1.0"
         CompatiblePlatformRange=">=0.1.0 &lt;1.0.0">
  <Platforms>
    <Platform Name="linux" />
    <Platform Name="windows" />
    <Platform Name="macos" />
  </Platforms>
  <Dependencies>
    <Dependency Name="NGIN.Core" VersionRange=">=0.1.0 &lt;1.0.0" Optional="false" />
  </Dependencies>
  <Bootstrap Mode="BuilderHookV1"
             EntryPoint="NGIN_Bootstrap_NGIN_Editor"
             AutoApply="true" />
  <Modules>
    <Module Name="Editor.Workspace"
            Family="Editor"
            Type="Runtime"
            LoadPhase="Application"
            Version="0.1.0"
            CompatiblePlatformRange=">=0.1.0 &lt;1.0.0"
            ReflectionRequired="false">
      <Platforms>
        <Platform Name="linux" />
        <Platform Name="windows" />
        <Platform Name="macos" />
      </Platforms>
      <Dependencies>
        <Dependency Name="Core.Hosting" RequiredVersion=">=0.1.0 &lt;1.0.0" Optional="false" />
      </Dependencies>
      <ProvidesServices>
        <Service Name="EditorWorkspace" />
      </ProvidesServices>
      <RequiresServices>
        <Service Name="Config" />
      </RequiresServices>
      <Capabilities>
        <Capability Name="Workspace" />
      </Capabilities>
    </Module>
  </Modules>
  <Plugins>
    <Plugin Name="NGIN.Diagnostics" Optional="true" />
  </Plugins>
  <Contents>
    <File Source="assets/default-layout.xml"
          Target="config/default-layout.xml"
          Kind="Config" />
  </Contents>
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

### Modules

Modules declared inside a package define runtime composition metadata.

Each `<Module>` may define:

- `Name` required
- `Family` optional, defaults to `Core`
- `Type` optional, defaults to `Runtime`
- `LoadPhase` optional, defaults to `CoreServices`
- `Version` optional but recommended
- `CompatiblePlatformRange` optional
- `ReflectionRequired` optional, defaults to `false`

Supported module child sections:

- `Platforms`
- `Dependencies`
- `ProvidesServices`
- `RequiresServices`
- `Capabilities`

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
- standalone runtime descriptor files are not part of the intended primary authoring model
- package names and versions identify reusable units
- content staging paths must be explicit and conflict-checked during composition

## Implementation Note

Current runtime code may still load lower-level dynamic descriptor files internally. That is an implementation seam, not the public authored model defined by this spec.
