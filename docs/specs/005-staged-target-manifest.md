# Spec 005: Launch Manifest

Status: Active
Last updated: 2026-04-28

## Purpose

This spec defines the `.nginlaunch` file emitted by `ngin build`.

The launch manifest is generated tooling metadata. It is used for local run,
debug, inspection, editor integration, and smoke tests. It is not an authored
input file and it is not a required production runtime dependency for shipped
applications.

## File Contract

- filename: `<Project>.<Profile>.nginlaunch`
- root element: `<LaunchManifest>`
- required root attributes:
  - `SchemaVersion="3"`
  - `Project`
  - `Profile`
  - `Type`
  - `BuildType`
  - `OperatingSystem`
  - `Architecture`

## Example

```xml
<?xml version="1.0" encoding="utf-8"?>
<LaunchManifest SchemaVersion="3"
                Project="App.Basic"
                Profile="Runtime"
                Type="Application"
                BuildType="Debug"
                OperatingSystem="linux"
                Architecture="x64">
  <Launch Executable="App.Basic" Target="App.Basic" WorkingDirectory="." />
  <Environment Name="development">
    <Variables />
    <Features />
  </Environment>
  <Inputs>
    <Input Kind="Config"
           Path="config/app.cfg"
           OwnerKind="project"
           Owner="App.Basic"
           ContentKind="config-input"
           Destination="config/app.cfg" />
  </Inputs>
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
- selected profile identity
- resolved build profile
- resolved operating system and architecture
- resolved launch working directory and selected executable
- resolved environment variables and features
- resolved packages, modules, plugins, and normalized inputs
- staged files

Secret environment variable values must not be serialized into the launch
manifest. A secret variable may be present only with redacted metadata such as
`Secret="true"` and, when applicable, `FromEnvironment`; raw secret values are
omitted.
