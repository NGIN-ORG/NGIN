# Spec 005: Launch Manifest

Status: Active
Last updated: 2026-04-24

## Purpose

This spec defines the `.nginlaunch` file emitted by `ngin build`.

The launch manifest is generated tooling metadata. It is used for local run,
debug, inspection, editor integration, and smoke tests. It is not an authored
input file and it is not a required production runtime dependency for shipped
applications.

## File Contract

- filename: `<Project>.<Configuration>.nginlaunch`
- root element: `<LaunchManifest>`
- required root attributes:
  - `SchemaVersion="2"`
  - `Project`
  - `Configuration`
  - `Type`
  - `BuildConfiguration`
  - `OperatingSystem`
  - `Architecture`

## Example

```xml
<?xml version="1.0" encoding="utf-8"?>
<LaunchManifest SchemaVersion="2"
                Project="App.Basic"
                Configuration="Runtime"
                Type="Application"
                BuildConfiguration="Debug"
                OperatingSystem="linux"
                Architecture="x64">
  <Launch Executable="App.Basic" Target="App.Basic" WorkingDirectory="." />
  <Environment Name="development">
    <Variables />
    <Features />
  </Environment>
  <StagedFiles>
    <File Kind="executable"
          Destination="/repo/.ngin/build/App.Basic/Runtime/bin/App.Basic"
          RelativeDestination="bin/App.Basic" />
  </StagedFiles>
</LaunchManifest>
```

## Required Contents

The launch manifest must capture:

- selected project identity
- selected configuration identity
- resolved build configuration
- resolved operating system and architecture
- resolved launch working directory and selected executable
- resolved environment variables and features
- resolved packages, modules, plugins, and config sources
- staged files
