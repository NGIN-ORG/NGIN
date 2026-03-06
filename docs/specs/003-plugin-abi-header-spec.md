# Spec 003: Package, Module, and Plugin Model

Status: Active design direction  
Last updated: 2026-03-07

## Summary

Packages are the main reusable unit in NGIN.

Modules and plugins are contents that packages may provide.

## Rules

- applications reference packages first
- packages provide modules, plugins, and staged content
- plugins are optional extensions inside the package model
- modules remain runtime-oriented composition units, not the primary distribution surface

## Authoring

Active authored files:

- `.nginproj`
- `.nginpkg`
- `.ngintarget`

The workspace catalogs remain internal metadata for validation and graph construction, not the main user-facing authoring surface.

## Compatibility

Before activating a target, tooling should validate:

- package existence
- version compatibility
- platform compatibility
- package dependency cycles
- package-provided module and plugin validity
- module dependency closure

## Non-Goals

- remote registry protocol
- publish/install distribution pipeline
- final dynamic plugin ABI details

Those come later, after the package-first model is proven by real products.
