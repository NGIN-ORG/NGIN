# Spec 003: Package Manifest and Runtime Contributions

Status: Active
Last updated: 2026-03-21

## Purpose

This spec defines the active V2 `.nginpkg` contract.

Packages are the reusable unit in NGIN.

## File Contract

- filename: `<PackageName>.nginpkg`
- root element: `<Package>`
- required root attributes:
  - `SchemaVersion="2"`
  - `Name`
  - `Version`

## Supported Top-Level Sections

- `Dependencies`
- `Artifacts`
- `Build`
- `Modules`
- `Plugins`
- `Contents`
- `Bootstrap`

`SourceBinding` is removed from the active package contract.

## Dependency Surface

Packages continue to use `PackageRef` inside `Dependencies`:

```xml
<Dependencies>
  <PackageRef Name="NGIN.Base" Version="0.1.0" Optional="false" />
</Dependencies>
```

## Ownership Rule

Packages describe reusable identity and exposed behavior.

Workspace-local source ownership is resolved outside the package manifest through:

- workspace `PackageSources`
- workspace `PackageProviders`
- package manifest location when no provider override exists
