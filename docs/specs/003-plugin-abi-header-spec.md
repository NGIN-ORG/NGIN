# Spec 003: Package, Module, and Plugin Model

Status: Active design direction  
Last updated: 2026-03-07

## Summary

Packages are the main reusable unit in NGIN.

Modules and plugins are contents that packages may provide.

## Rules

- applications reference packages first
- packages provide modules, plugins, and staged content
- plugins are optional extensions inside the package model
- modules remain runtime-oriented composition units, not the primary distribution surface

## Authoring

Active authored files:

- `.nginproj`
- `.nginpkg`
- `.ngintarget`

The workspace catalogs remain internal metadata for validation and graph construction, not the main user-facing authoring surface.

Runtime-side dynamic discovery uses XML descriptor files:

- `.module.xml`
- `.plugin-module.xml`

These are lower-level runtime descriptors consumed by `NGIN.Core` loader seams. They are not the main authored package/project surface.

## Dynamic Module Descriptor XML

Dynamic module descriptors are discovered from `KernelHostConfig.pluginSearchPaths`.

Filename rules:

- files must end with `.module.xml` or `.plugin-module.xml`
- the descriptor root must be `<Module>`
- the runtime derives `pluginName` from the parent directory name when the descriptor does not provide one elsewhere

Descriptor root attributes:

- `Name` required
- `Family` optional, defaults to `Core`
- `Type` optional, defaults to `Runtime`
- `LoadPhase` optional, defaults to `CoreServices`
- `Version` optional
- `CompatiblePlatformRange` optional
- `ReflectionRequired` optional, defaults to `false`

Supported child sections:

- `<Platforms><Platform Name="..." /></Platforms>`
- `<Dependencies><Dependency Name="..." RequiredVersion="..." Optional="false" /></Dependencies>`
- `<ProvidesServices><Service Name="..." /></ProvidesServices>`
- `<RequiresServices><Service Name="..." /></RequiresServices>`
- `<Capabilities><Capability Name="..." /></Capabilities>`

Example:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Module Name="Core.DynamicDemo"
        Family="Core"
        Type="Runtime"
        LoadPhase="CoreServices"
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
  <RequiresServices>
    <Service Name="Config" />
  </RequiresServices>
  <ProvidesServices>
    <Service Name="DynamicProbe" />
  </ProvidesServices>
  <Capabilities>
    <Capability Name="Diagnostics" />
  </Capabilities>
</Module>
```

Validation rules:

- `Name` must be present
- version and version-range values must parse as valid semantic-version expressions
- dependency names are required for each `<Dependency>`
- descriptors participate in the same module dependency, family, and phase checks as static modules after discovery

Current scope:

- descriptor discovery is XML-only
- binary loading remains behind the `IPluginBinaryLoader` seam
- final published plugin ABI/package distribution rules still come later

## Compatibility

Before activating a target, tooling should validate:

- package existence
- version compatibility
- platform compatibility
- package dependency cycles
- package-provided module and plugin validity
- module dependency closure

## Non-Goals

- remote registry protocol
- publish/install distribution pipeline
- final dynamic plugin ABI details

Those come later, after the package-first model is proven by real products.
