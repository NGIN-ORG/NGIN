# Spec 006: CLI Contract

Status: Active
Last updated: 2026-04-28

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
- `ngin package list`
- `ngin package show <Package>`
- `ngin settings init [--project <file>]`
- `ngin variables explain [--project <file>] [--profile <name>]`
- `ngin workspace list`
- `ngin workspace status`
- `ngin workspace doctor`

Removed commands:

- `ngin project ...`
- `ngin workspace sync`

## Behavior

- `--profile` always selects the project profile
- backend build type comes from the selected project profile’s `BuildType`
- `ngin configure` resolves the selected composition, generates backend CMake input, runs CMake configure, and emits generated build metadata such as `compile_commands.json` without staging runtime outputs
- `ngin build` emits `.nginlaunch`
- `ngin build` configures the generated backend build when needed before building and staging artifacts
- `ngin build` remains incremental and should not aggressively remove unrelated files outside NGIN-owned stale outputs
- generated CMake builds may resolve backend tools from explicit environment overrides, bundled tools under `Tools/ThirdParty/BuildTools`, or `PATH`
- `ngin clean` removes NGIN-owned generated artifacts for the selected build scope
- `ngin rebuild` is equivalent to `ngin clean` followed by `ngin build`
- `ngin run` consumes the generated `.nginlaunch`
- a workspace is optional

## Inspection Direction

- `ngin graph` is the active structural inspection command for resolved composition
- package inspection commands expose reusable package identity and declared package data
- `ngin variables explain` shows the selected project environment variables,
  their resolved value source, and redacts secret values as `<secret>`
- `ngin settings init` creates `.ngin/local/user.nginsettings` under the
  workspace root and ensures local settings are ignored by source control
- future CLI inspection may add more direct provenance commands without changing the authored model split
