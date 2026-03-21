# Spec 011: Workspace Manifest

Status: Active
Last updated: 2026-03-21

## Purpose

This spec defines the optional `.ngin` workspace manifest.

## File Contract

- filename: `<Name>.ngin`
- root element: `<Workspace>`
- required root attributes:
  - `SchemaVersion="2"`
  - `Name`

## Supported Sections

- `PackageSources`
- `PackageProviders`
- `Projects`

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
