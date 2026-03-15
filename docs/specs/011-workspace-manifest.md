# Spec 011: Workspace Manifest

Status: Active
Last updated: 2026-03-15

## Purpose

This spec defines the active `.ngin` workspace file format.

The workspace manifest is the developer-facing authority for:

- project discovery
- package source discovery
- workspace platform version selection

## File Contract

Filename:

- `<WorkspaceName>.ngin`

Root element:

- `<Workspace>`

Required root attributes:

- `SchemaVersion`
- `Name`

Optional root attributes:

- `PlatformVersion`

## Structure

```xml
<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="1"
           Name="NGIN.Workspace"
           PlatformVersion="0.1.0">
  <PackageSources>
    <PackageSource Path="Packages" />
  </PackageSources>
  <Projects>
    <Project Path="Examples/App.Basic/App.Basic.nginproj" />
  </Projects>
</Workspace>
```

## Sections

### PackageSources

`PackageSources` declares the package source roots visible to the CLI.

Each `<PackageSource>` may define:

- `Path` required

### Projects

`Projects` declares the authored projects that belong to the workspace.

Each `<Project>` may define:

- `Path` required

## Rules

- `.ngin` is an authored workspace file
- relative `Path` values are resolved relative to the workspace file location
- the CLI discovers the nearest `.ngin` file and treats it as the workspace authority for workspace commands and package-source visibility
- workspaces must declare both `PackageSources` and `Projects`
- `SchemaVersion` is required and must be supported by the CLI

## Intent

The workspace manifest is a container for developer workflow and project discovery.
It is not a surrogate application project and it does not replace `.nginproj` or `.nginpkg`.