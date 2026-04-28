# Spec 011: Workspace Manifest

Status: Active
Last updated: 2026-04-28

## Purpose

This spec defines the optional `.ngin` workspace manifest.

## File Contract

- filename: `<Name>.ngin`
- root element: `<Workspace>`
- required root attributes:
  - `SchemaVersion="3"`
  - `Name`

## Supported Sections

- `Includes`
- `Defaults`
- `Platforms`
- `ProjectTemplates`
- `ProfileTemplates`
- `PackageSources`
- `PackageProviders`
- `Projects`

## Model Contributions

Workspace V3 may include shared `.nginmodel` files and may declare model
contributions directly:

```xml
<Workspace SchemaVersion="3" Name="Examples">
  <Includes>
    <Include Path="Common.nginmodel" />
  </Includes>
  <Defaults BuildType="Debug" Platform="linux-x64" />
</Workspace>
```

Included model paths are relative to the declaring workspace or model file.
Includes resolve depth-first in declaration order. Missing include files and
include cycles are hard validation errors.

Workspace model contributions apply to projects loaded through workspace
resolution. Project-local includes, defaults, templates, and explicit profile
values override workspace-provided defaults where scalar values conflict.

## Package Providers

Package providers let a workspace map a package name to a local source root:

```xml
<PackageProviders>
  <PackageProvider Name="NGIN.ECS" Root="Dependencies/NGIN/NGIN.ECS" />
</PackageProviders>
```

Resolution rule:

- provider root override first
- otherwise use the package manifest directory

## Workspace Optionality

If no workspace manifest is present, tooling may still operate on a project directly and may discover packages by scanning ancestor `Packages/` directories.
