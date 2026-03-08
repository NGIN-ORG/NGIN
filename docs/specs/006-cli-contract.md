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

- `ngin workspace list`
- `ngin workspace status`
- `ngin workspace doctor`
- `ngin workspace sync`
- `ngin project validate --project <file.nginproj> --target <Target>`
- `ngin project graph --project <file.nginproj> --target <Target>`
- `ngin project build --project <file.nginproj> --target <Target> --output <dir>`
- `ngin package list`
- `ngin package show <Package>`

Future command:

- `ngin run`

## Behavior

### workspace status / doctor / sync

Operate on the current umbrella workspace layout, component repo availability, and release-pinned dependency checkouts.

### project validate

Loads a project, selects a target, resolves composition, and reports success, warnings, or errors.

### project graph

Loads a project, selects a target, resolves composition, and prints the resolved package/module/plugin graph.

### project build

Loads a project, selects a target, resolves composition, stages files into an output directory, and emits a `.ngintarget` file.

### package list

Lists packages known to the current workspace metadata.

### package show

Prints package metadata relevant to composition and staging.

## Discovery

- `--project` is the explicit contract
- implementations may also support walking upward to discover the nearest `.nginproj`
- workspace commands may also accept a dependency checkout override directory for sync/status/doctor flows

## Rules

- the CLI should not require a user-facing lockfile
- the CLI should present packages as the main reusable unit
- the CLI should fail early on invalid composition state
- the CLI should expose workspace/package/build concerns through grouped commands rather than a flat tool surface
