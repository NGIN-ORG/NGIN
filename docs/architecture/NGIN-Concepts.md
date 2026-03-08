# NGIN Concepts

Status: Active

NGIN should stay understandable through a very small model.

The active spec source for these concepts is [Spec 001: Core Concepts and Vocabulary](/home/berggrenmille/NGIN/docs/specs/001-core-concepts.md).

## Project

The top-level application definition.

A project answers: what application am I building, and which targets does it expose?

Authoring file:

- `.nginproj`

## Target

One concrete application variant inside a project.

Examples:

- game runtime
- editor
- CLI tool
- service

A target selects packages, profile, platform, and advanced module/plugin overrides.

## Package

The main reusable unit users reference.

A package may provide:

- libraries and executables
- modules
- plugins
- staged content files
- bootstrap metadata

Users should think in packages first.

`.nginpkg` is the authored package manifest. A future `.nginpack` archive is the planned installable package form.

## Module

A runtime composition unit provided by packages.

Modules matter for dependency ordering and host startup. They are not the primary user-facing distribution unit.

## Plugin

An optional extension provided by packages.

Plugins are subordinate to the package model, not a separate top-level ecosystem.

## Host

The runtime container built from a resolved target.

The host starts the resolved module set and owns the application lifecycle.

In current implementation terms, `NGIN.Core` is the active host.

## Staged Target

The generated target artifact emitted by `ngin build`.

`.ngintarget` is not a primary authored file. It is the bridge between composition and runtime.

At runtime, lower-level XML descriptor files such as `.module.xml` may still exist as implementation details. They are not the main authored package/project manifests.
