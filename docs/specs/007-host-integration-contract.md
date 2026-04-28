# Spec 007: Host Integration Contract

Status: Active
Last updated: 2026-04-28

## Purpose

This spec defines how `NGIN.Core` relates to the authored V3 model and generated
tooling metadata.

`NGIN.Core` is optional. Plain native applications may be built and staged by
NGIN without linking or using `NGIN.Core`.

## Application Builder Contract

Public application APIs use profile terminology:

- `ProfileDefinition`
- `SetProfile(...)`
- `GetProfileName()`
- `ProfileName()`

## Host Inputs

The host may derive development-time launch behavior from:

- selected profile
- generated launch manifest

The host must be usable without source manifests or `.nginlaunch` at production
runtime. Hosted applications should be able to configure startup from code,
staged config/content, command line arguments, environment variables, and
explicit runtime metadata chosen by the application.

When the host consumes selected profile or generated launch metadata, it
must honor:

- `Environment`
- `WorkingDirectory`
- `EnableReflection`
- selected executable

The authored model does not classify the application by host category. Runtime behavior is described through launch metadata, selected environment, and resolved runtime composition.

Advanced project runtime metadata remains supported, but it is optional.
