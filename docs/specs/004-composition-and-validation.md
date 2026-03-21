# Spec 004: Composition and Validation

Status: Active
Last updated: 2026-03-21

## Purpose

This spec defines resolution and validation for a selected project configuration.

## Resolution Inputs

Composition starts from:

- one project
- one selected configuration
- optional workspace context

## Resolution Steps

1. load the selected project
2. resolve the selected configuration
3. merge root and configuration-level references
4. resolve project references recursively
5. resolve package references
6. apply config source, module, and plugin overlays
7. determine output and launch selection

## Validation Rules

Validation must reject:

- unknown selected configuration
- duplicate configuration names in one project
- invalid host profile values
- unresolved required project references
- unresolved required package references
- duplicate selected executable names that cannot be disambiguated

Validation may succeed without a runnable executable when the project is valid but does not resolve to a launchable output.
