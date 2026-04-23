# Spec 004: Composition and Validation

Status: Active
Last updated: 2026-04-23

## Purpose

This spec defines resolution and validation for a selected project configuration.

NGIN prefers explicit validation failure over implicit or ambiguous runtime resolution.

## Resolution Inputs

Composition starts from:

- one project
- one selected configuration
- optional workspace context

The output of this process is a resolved composition that determines selected launch data, staged files, and the runtime contributions visible to the host.

## Resolution Steps

1. load the selected project
2. resolve the selected configuration
3. resolve the selected environment layer
4. merge root, environment, and configuration-level references
5. resolve project references recursively
6. resolve package references
7. apply config source, content, module, plugin, variable, and feature overlays
8. determine output and launch selection

## Conflict Rule

Composition resolution must be deterministic.

When multiple contributions produce an ambiguous launch or runtime result and the ambiguity cannot be resolved by the active authored model, validation must fail rather than silently picking an implicit winner.

Project-level authored selection or override data is the intended escape hatch for resolving otherwise valid competing contributions.

## Provenance Expectation

Resolved composition data should preserve enough provenance for tooling to explain why a package, module, plugin, config source, staged file, or launch selection is present.

`ngin graph` is the current structural inspection surface. Future inspection commands may expose more direct causal or "why is this here" views over the same composition data.

## Validation Rules

Validation must reject:

- unknown selected configuration
- unknown selected environment
- duplicate configuration names in one project
- duplicate environment names in one project
- invalid operating system values
- invalid architecture values
- invalid `Type` and `Output Kind` pairings
- launch metadata on non-launchable projects
- unresolved required project references
- unresolved required package references
- referenced project target mismatch on operating system or architecture
- duplicate selected executable names that cannot be disambiguated
- ambiguous runtime or launch contributions that the authored model does not explicitly resolve

Validation may succeed without a runnable executable when the project is valid but does not resolve to a launchable output.
