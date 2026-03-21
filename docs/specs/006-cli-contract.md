# Spec 006: CLI Contract

Status: Active
Last updated: 2026-03-21

## Purpose

This spec defines the active user-facing surface of the native `ngin` CLI.

## Commands

Stable active commands:

- `ngin build [--project <file>] [--configuration <name>] [--output <dir>]`
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
- `ngin run` consumes the generated `.nginlaunch`
- a workspace is optional
