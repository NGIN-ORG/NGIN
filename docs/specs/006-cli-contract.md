# Spec 006: CLI Contract

Status: Active
Last updated: 2026-04-30

## Purpose

This spec defines the active user-facing surface of the native `ngin` CLI.

## Commands

Stable active commands:

- `ngin build [--project <file>] [--profile <name>] [--output <dir>]`
- `ngin configure [--project <file>] [--profile <name>] [--output <dir>]`
- `ngin clean [--project <file>] [--profile <name>] [--output <dir>]`
- `ngin rebuild [--project <file>] [--profile <name>] [--output <dir>]`
- `ngin run [--project <file>] [--profile <name>] [--output <dir>] [-- <args...>]`
- `ngin validate [--project <file>] [--profile <name>]`
- `ngin graph [--project <file>] [--profile <name>]`
- `ngin inspect [--project <file>] [--profile <name>] [--output <dir>] --format json`
- `ngin package list`
- `ngin package show <Package>`
- `ngin package lock [--project <file>] [--profile <name>] [--output <file>]`
- `ngin package verify-lock [--project <file>] [--profile <name>] [--lock <file>]`
- `ngin explain condition <Name> [--project <file>] [--profile <name>]`
- `ngin explain package-feature <Package> <Feature> [--project <file>] [--profile <name>]`
- `ngin explain generator <Name> [--project <file>] [--profile <name>]`
- `ngin settings init [--project <file>]`
- `ngin variables explain [--project <file>] [--profile <name>]`
- `ngin workspace list`
- `ngin workspace status`
- `ngin workspace doctor`

Removed commands:

- `ngin project ...`
- `ngin workspace sync`
- `ngin metagen`

## Behavior

- `--profile` always selects the project profile
- backend build type comes from the selected project profile’s `BuildType`
- `ngin configure` resolves the selected composition, writes generator context
  files, runs selected package/local command generators, generates backend CMake
  input, runs CMake configure, and emits generated build metadata such as
  `compile_commands.json` without staging runtime outputs
- `ngin build` emits `.nginlaunch`
- `ngin build` configures the generated backend build when needed before building and staging artifacts
- `ngin build` remains incremental and should not aggressively remove unrelated files outside NGIN-owned stale outputs
- generated CMake builds may resolve backend tools from explicit environment overrides, bundled tools under `Tools/ThirdParty/BuildTools`, or `PATH`
- `ngin clean` removes NGIN-owned generated artifacts for the selected build scope
- `ngin rebuild` is equivalent to `ngin clean` followed by `ngin build`
- `ngin run` consumes the generated `.nginlaunch`
- `ngin inspect --format json` resolves the selected project/profile and emits
  a schema-versioned JSON payload without configuring, building, running
  generators, writing lock files, or mutating repository state
- a workspace is optional

## Event Stream Output

Build-backed lifecycle commands support a stable JSON Lines event stream for
editor and CI consumers:

```bash
ngin build --project <file.nginproj> --profile <name> --events jsonl
```

`--events none` keeps normal human output. `--events jsonl` writes only
`NGIN.CLI.Event` JSON objects to stdout, one object per line. Human summaries,
terminal color, and backend passthrough logs are not mixed into stdout in this
mode. `--format jsonl` and `--ui jsonl` are accepted as aliases for selecting
the event stream where common output flags are parsed.

The event envelope is versioned independently from manifest and composition
schemas:

```json
{"schemaVersion":"1.0","kind":"NGIN.CLI.Event","sequence":1,"timestamp":"2026-05-12T00:00:00.000Z","type":"command.started","command":"build","data":{}}
```

Initial event types are `command.started`, `command.selection`,
`phase.started`, `phase.completed`, `phase.failed`, `backend.output`,
`diagnostic`, `artifact.produced`, `summary`, and `command.completed`.
`sequence` is monotonically increasing per process. Consumers must ignore
unknown event types and fields. Additive optional fields are allowed in
`schemaVersion` `1.x`; removing or renaming fields requires a new major event
schema.

Backend output events follow `--backend-output`: compact JSONL includes backend
output only when a backend phase fails, stream mode includes captured backend
output events on success, and silent mode suppresses backend output events.

## Inspection Direction

- `ngin graph` is the active structural inspection command for resolved composition
- `ngin inspect --format json` is the machine-readable inspection command for
  editor tooling; payload schema version `1` includes selected context,
  packages, package edges, feature states, capabilities, generators, typed
  inputs, launch metadata, staged-file metadata when known, redacted
  environment variables, diagnostics, and lock-file status
- package inspection commands expose reusable package identity and declared package data
- `ngin package lock` writes a deterministic local `ngin.lock` for the selected
  package graph; `ngin package verify-lock` compares the current local
  resolution with that file
- `ngin explain package-feature` explains feature selection, dependencies,
  capabilities, and contribution counts for one package feature
- `ngin explain generator` explains a selected generator declaration, its
  owner, tool origin, declared inputs, outputs, and arguments
- `ngin variables explain` shows the selected project environment variables,
  their resolved value source, and redacts secret values as `<secret>`
- `ngin settings init` creates `.ngin/local/user.nginsettings` under the
  workspace root and ensures local settings are ignored by source control
- future CLI inspection may add more direct provenance commands without changing the authored model split
