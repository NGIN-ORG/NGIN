# Spec 010: Workspace and Project Model

Status: Active
Last updated: 2026-03-21

## Purpose

This spec defines the authored model split between workspace, project, and package.

## Rules

- `.nginproj` is the normal authored unit for applications, tools, and libraries
- `.nginpkg` is the reusable dependency unit
- `.ngin` is optional
- separate executables should usually be separate projects
- configurations should hold narrow selection and override data
- NGIN does not model application category as a configuration property
- launch metadata belongs to launchable projects

## Consequences

- engine library, game client, and headless server are usually separate projects
- `Debug`, `Shipping`, `Diagnostics`, and similar modes are configurations
- package-local source binding is not part of the active reusable contract
