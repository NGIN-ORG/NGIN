# Spec 001: Core Concepts and Vocabulary

Status: Active
Last updated: 2026-03-07

## Purpose

This spec defines the core NGIN model and the vocabulary used by all other active specs.

NGIN should remain understandable through a small number of concepts:

- `Project`
- `Target`
- `Package`
- `Module`
- `Plugin`
- `Host`
- `Staged Target`

## Definitions

### Project

The top-level application definition.

A project answers:

- what application is being built
- which targets it exposes
- which package references, config sources, and overrides belong to each target

Projects are authored in `.nginproj` files.

### Target

One concrete application variant inside a project.

Examples:

- game runtime
- editor
- program
- service
- developer tool

A target selects packages and host-facing metadata such as platform, profile, environment, and working directory.

### Package

The main reusable unit users reference.

A package may provide:

- runtime modules
- optional plugins
- staged content files
- bootstrap metadata
- runtime contribution metadata

Packages are authored in `.nginpkg` files.

### Module

A runtime composition unit provided by a package.

Modules participate in dependency ordering, lifecycle orchestration, service registration, and host startup.

Modules are not the primary user-facing distribution surface.

### Plugin

An optional extension provided by a package.

Plugins are subordinate to the package model. They are not a separate top-level packaging model.

### Host

The runtime container built from a resolved target.

In the current platform implementation, `NGIN.Core` is the active host implementation.

### Staged Target

A staged target is the output of `ngin build`.

It is represented by a `.ngintarget` file and the corresponding staged directory layout.

A staged target is a build artifact, not a primary authored source file.

## Authoring Rules

- users author projects and packages
- targets live inside projects
- packages are the primary reusable unit
- modules and plugins are usually declared inside packages, not as separate top-level authored files
- the host consumes a resolved target model, not unrelated authoring fragments

## Public File Types

User-authored files:

- `.nginproj`
- `.nginpkg`

Generated files:

- `.ngintarget`

Lower-level runtime descriptors may exist as implementation details, but they are not part of the intended primary authoring model.

## Composition Flow

The active platform flow is:

1. load a project
2. select a target
3. resolve packages
4. derive modules and plugins from those packages
5. validate the resolved graph
6. build a staged target
7. hand the staged target to the host or a future runner
