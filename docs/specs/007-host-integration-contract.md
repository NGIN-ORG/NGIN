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
- generated launch manifest

The host must honor:

- `Environment`
- `WorkingDirectory`
- `EnableReflection`
- selected executable

The authored model does not classify the application by host category. Runtime behavior is described through launch metadata, selected environment, and resolved runtime composition.

Advanced project runtime metadata remains supported, but it is optional.
