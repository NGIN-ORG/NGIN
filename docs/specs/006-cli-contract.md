# Spec 006: CLI Contract

Status: Active
Last updated: 2026-03-10

## Purpose

This spec defines the user-facing contract of the native `ngin` CLI.

## Scope

The CLI owns:

- workspace loading
- project and package loading
- variant selection
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
- `ngin project validate --project <file.nginproj> --variant <name>`
- `ngin project graph --project <file.nginproj> --variant <name>`
- `ngin project build --project <file.nginproj> --variant <name> --output <dir>`
- `ngin package list`
- `ngin package show <Package>`

Future command:

- `ngin run`

Planned package lifecycle commands after the distribution model is specified:

- `ngin package pack`
- `ngin package inspect`
- `ngin package verify`
- `ngin package install`
- `ngin package uninstall`
- `ngin package restore`

## Behavior

### workspace status / doctor / sync

Operate on the current `.ngin` workspace, package source roots, and the component repos available to that workspace.

### project validate

Loads a project, selects a variant, resolves composition, and reports success, warnings, or errors.

### project graph

Loads a project, selects a variant, resolves composition, and prints the resolved project/package/module graph.

### project build

Loads a project, selects a variant, resolves composition, resolves artifact and executable candidates, invokes the active backend when needed, stages outputs and content into an output directory, and emits a `.ngintarget` file.

### package list

Lists packages visible through the current workspace's declared package source roots.

### package show

Prints package metadata relevant to composition, artifacts, and staging.

The current active CLI contract works on authored manifests and workspace-declared package sources. Package distribution and installation commands are planned separately from the current build/graph/validate surface.

## Discovery

- `--project` is the explicit contract
- implementations may also support walking upward to discover the nearest `.nginproj`
- workspace commands discover the nearest `.ngin` file and use it as the workspace authority

## Rules

- the CLI should not require a user-facing lockfile
- the CLI should present projects as the main authored unit and packages as the main reusable unit
- the CLI should fail early on invalid composition state
- the CLI should expose workspace/project/package/build concerns through grouped commands rather than a flat tool surface
