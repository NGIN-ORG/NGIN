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
3. merge root and configuration-level references
4. resolve project references recursively
5. resolve package references
6. apply config source, module, and plugin overlays
7. determine output and launch selection

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
- duplicate configuration names in one project
- invalid host profile values
- unresolved required project references
- unresolved required package references
- duplicate selected executable names that cannot be disambiguated
- ambiguous runtime or launch contributions that the authored model does not explicitly resolve

Validation may succeed without a runnable executable when the project is valid but does not resolve to a launchable output.
