# Spec 002: Host and Builder Model

Status: Active normative spec  
Last updated: 2026-03-07

## Summary

NGIN applications start from a common host model.

The host model is:

1. select a project
2. select a target
3. resolve packages
4. derive modules and plugins from those packages
5. build the host

## Concepts

- `Project`: top-level application definition
- `Target`: one concrete application variant
- `Package`: main reusable composition unit
- `Module`: runtime contribution provided by packages
- `Plugin`: optional extension provided by packages
- `Host`: runtime container

## Rules

- projects are authored in `.nginproj`
- packages are authored in `.nginpkg`
- packages are the default user-facing composition unit
- direct module/plugin enablement is an advanced target override
- lockfiles are not part of the active public workflow

## Runtime Role

`NGIN.Core` is the current host implementation.

It should consume the resolved package-first model without exposing extra workspace-only concepts as part of the runtime contract.
