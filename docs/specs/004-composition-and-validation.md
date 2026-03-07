# Spec 004: Composition and Validation Rules

Status: Active
Last updated: 2026-03-07

## Purpose

This spec defines how a project target becomes a resolved application model.

## Resolution Pipeline

The active pipeline is:

1. load a project manifest
2. select a target
3. resolve the package dependency graph
4. collect modules, plugins, bootstrap metadata, and content from resolved packages
5. apply target-level module and plugin overrides
6. validate the resulting graph
7. produce a resolved target model for graphing, staging, and host startup

## Resolution Rules

### Package Resolution

- all required package references must resolve
- optional package references may be skipped with a warning
- dependency cycles are invalid
- package version constraints must be satisfied
- package platform compatibility must be satisfied

### Module Resolution

- modules are collected from resolved packages
- module names must be unique in the resolved target
- target-level module overrides may enable or disable modules after package collection
- module dependency closure must be complete after overrides are applied

### Plugin Resolution

- plugins are collected from resolved packages
- target-level plugin overrides may enable or disable plugins after package collection
- plugin activation must not violate package or module constraints

### Content Resolution

- staged content files are collected from resolved packages
- project config sources are collected from the selected target
- staged output destination paths must be unique unless an explicit override rule is defined later

## Validation Failures

Validation must fail on:

- missing required package
- package dependency cycle
- package version mismatch
- package platform mismatch
- duplicate provided module name
- unresolved module dependency
- invalid module family or phase ordering
- duplicate staged output path
- invalid bootstrap metadata
- invalid config source or content declaration

Validation may warn on:

- missing optional package
- disabled optional plugin
- package content not used by the selected target profile

## Outputs

Successful composition produces:

- resolved package set
- resolved module set
- resolved plugin set
- staged content map
- target-level host metadata

That resolved model is the input to:

- `ngin graph`
- `ngin build`
- future `ngin run`
- host integration
