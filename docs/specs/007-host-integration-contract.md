# Spec 007: Host Integration Contract

Status: Active
Last updated: 2026-03-21

## Purpose

This spec defines how `NGIN.Core` consumes the authored V2 model and generated launch manifest.

## Application Builder Contract

Public application APIs use configuration terminology:

- `ConfigurationDefinition`
- `SetConfiguration(...)`
- `GetConfigurationName()`
- `ConfigurationName()`

## Host Inputs

The host must derive runtime launch behavior from:

- selected configuration
- root host defaults
- generated launch manifest

The host must honor:

- `HostProfile`
- `Environment`
- `WorkingDirectory`
- `EnableReflection`
- selected executable

Advanced project runtime metadata remains supported, but it is optional.
