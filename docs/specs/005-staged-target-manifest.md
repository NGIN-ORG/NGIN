# Spec 005: Staged Target Manifest

Status: Active
Last updated: 2026-03-07

## Purpose

This spec defines the `.ngintarget` file emitted by `ngin build`.

The staged target manifest is the handoff artifact between tooling and runtime.

## File Contract

Filename:

- `<TargetName>.ngintarget`

Root element:

- `<TargetLayout>`

Required root attributes:

- `SchemaVersion`
- `Project`
- `Target`

## Structure

```xml
<?xml version="1.0" encoding="utf-8"?>
<TargetLayout SchemaVersion="1"
              Project="Sandbox.Game"
              Target="Game"
              Type="Runtime"
              Profile="Game"
              Platform="linux-x64">
  <Runtime WorkingDirectory="." Environment="Dev" />
  <Packages>
    <Package Name="NGIN.Core" Version="0.1.0" />
    <Package Name="NGIN.ECS" Version="0.1.0" />
  </Packages>
  <Modules>
    <Module Name="Core.Hosting" />
    <Module Name="Domain.ECS" />
  </Modules>
  <Plugins />
  <ConfigSources>
    <Config Source="config/game.xml" />
  </ConfigSources>
  <StagedFiles>
    <File Source="assets/default-layout.xml"
          Destination="config/default-layout.xml"
          Kind="Config" />
  </StagedFiles>
</TargetLayout>
```

## Required Content

A staged target manifest should contain:

- source project identity
- selected target identity
- host-facing metadata such as type, profile, platform, working directory, and environment
- resolved packages
- resolved modules
- resolved plugins
- config sources
- staged file map

## Rules

- `.ngintarget` is generated, not authored
- the file must reflect the resolved target model used for staging
- staged file destinations must be deterministic
- future run support should consume the staged target contract rather than re-resolving authored manifests during launch

## Intent

NGIN should separate authoring from runtime execution:

- projects and packages define intent
- composition resolves intent
- `.ngintarget` captures the staged result
