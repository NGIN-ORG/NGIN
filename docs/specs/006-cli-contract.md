# Spec 006: CLI Contract

Status: Active
Last updated: 2026-04-23

## Purpose

This spec defines the active user-facing surface of the native `ngin` CLI.

## Commands

Stable active commands:

- `ngin build [--project <file>] [--configuration <name>] [--output <dir>]`
- `ngin clean [--project <file>] [--configuration <name>] [--output <dir>]`
- `ngin rebuild [--project <file>] [--configuration <name>] [--output <dir>]`
- `ngin run [--project <file>] [--configuration <name>] [--output <dir>] [-- <args...>]`
- `ngin validate [--project <file>] [--configuration <name>]`
- `ngin graph [--project <file>] [--configuration <name>]`
- `ngin package list`
- `ngin package show <Package>`
- `ngin workspace list`
- `ngin workspace status`
- `ngin workspace doctor`

Removed commands:

- `ngin project ...`
- `ngin workspace sync`

## Behavior

- `--configuration` always selects the project configuration
- build configuration comes from the selected project configuration’s `BuildConfiguration`
- `ngin build` emits `.nginlaunch`
- `ngin build` remains incremental and should not aggressively remove unrelated files outside NGIN-owned stale outputs
- `ngin clean` removes NGIN-owned generated artifacts for the selected build scope
- `ngin rebuild` is equivalent to `ngin clean` followed by `ngin build`
- `ngin run` consumes the generated `.nginlaunch`
- a workspace is optional

## Inspection Direction

- `ngin graph` is the active structural inspection command for resolved composition
- package inspection commands expose reusable package identity and declared package data
- future CLI inspection may add more direct provenance or explanation commands without changing the authored model split
