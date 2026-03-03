# NGIN Architecture (Umbrella Workspace)

## Purpose

`NGIN` is the top-level workspace and platform release repository for the NGIN application engine platform.

It coordinates a set of independently versioned component repositories and defines the architecture and compatibility rules that make them operate as one platform.

## Naming Decision

- Umbrella workspace/repo: `NGIN`
- Component repos/libraries: `NGIN.Base`, `NGIN.Log`, `NGIN.Reflection`, `NGIN.ECS`, `NGIN.Core`, ...
- Avoid creating a separate runtime library named only `NGIN` to prevent ambiguity.
- Use names like `NGIN.Runtime` or `NGIN.Kernel` for the future runtime kernel implementation.

## Current Repository Model

NGIN uses a federated/polyrepo model:

- Each component repo remains independently buildable and installable.
- The `NGIN` workspace publishes a single platform version that pins compatible component refs.
- Integration and architecture rules live in this repo.

This matches the current state of the local workspace, where the component directories are separate Git repositories.

## Release Model

### Platform Version

The umbrella workspace owns the platform release version (example: `0.1.0-alpha.1`).

### Component Versions

Components keep their own package/library versions (for example `NGIN.Base` can remain `0.1` while the platform advances).

### Compatibility Source of Truth

`manifests/platform-release.json` defines the compatible set of refs and roles for each component.

## Architecture Layers

Conceptual layering for the platform:

1. Applications
2. Domain engines (`Game`, `Simulation`, `CAD`, etc.)
3. NGIN platform runtime/tooling
4. OS / hardware abstractions

Detailed dependency rules and module boundaries are specified in `docs/specs/001-module-dependency-graph.md`.

## Implementation Strategy (Near Term)

1. Stabilize terminology and module graph rules.
2. Add manifest-driven workspace sync and integration CI.
3. Design runtime kernel and plugin ABI on top of locked layering.
4. Add editor/tool host architecture after runtime and plugin boundaries are stable.
