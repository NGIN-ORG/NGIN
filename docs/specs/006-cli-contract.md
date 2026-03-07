# Spec 006: CLI Contract

Status: Active
Last updated: 2026-03-07

## Purpose

This spec defines the user-facing contract of the native `ngin` CLI.

## Scope

The CLI owns:

- project and package loading
- target selection
- composition validation
- graph inspection
- staged target generation

The CLI does not define the runtime host itself.

## Commands

Stable active commands:

- `ngin list`
- `ngin status`
- `ngin doctor`
- `ngin sync`
- `ngin validate --project <file.nginproj> --target <Target>`
- `ngin graph --project <file.nginproj> --target <Target>`
- `ngin build --project <file.nginproj> --target <Target> --output <dir>`
- `ngin package list`
- `ngin package show <Package>`

Future command:

- `ngin run`

## Behavior

### validate

Loads a project, selects a target, resolves composition, and reports success, warnings, or errors.

### graph

Loads a project, selects a target, resolves composition, and prints the resolved package/module/plugin graph.

### build

Loads a project, selects a target, resolves composition, stages files into an output directory, and emits a `.ngintarget` file.

### package list

Lists packages known to the current workspace metadata.

### package show

Prints package metadata relevant to composition and staging.

## Discovery

- `--project` is the explicit contract
- implementations may also support walking upward to discover the nearest `.nginproj`

## Rules

- the CLI should not require a user-facing lockfile
- the CLI should present packages as the main reusable unit
- the CLI should fail early on invalid composition state
