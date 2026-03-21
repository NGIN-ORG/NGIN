# Spec 005: Launch Manifest

Status: Active
Last updated: 2026-03-21

## Purpose

This spec defines the `.nginlaunch` file emitted by `ngin build`.

## File Contract

- filename: `<Project>.<Configuration>.nginlaunch`
- root element: `<LaunchManifest>`
- required root attributes:
  - `SchemaVersion="2"`
  - `Project`
  - `Configuration`
  - `Type`
  - `BuildConfiguration`
  - `Platform`

## Example

```xml
<?xml version="1.0" encoding="utf-8"?>
<LaunchManifest SchemaVersion="2"
                Project="App.Basic"
                Configuration="Runtime"
                Type="Application"
                BuildConfiguration="Debug"
                HostProfile="ConsoleApp"
                Platform="Linux">
  <Runtime WorkingDirectory="." Environment="Dev" />
  <SelectedExecutable Name="App.Basic" Target="App.Basic" />
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
- resolved platform
- resolved working directory and environment
- selected executable
- resolved packages, modules, plugins, and config sources
- staged files
