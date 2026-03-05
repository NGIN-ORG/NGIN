# Spec 003: Package, Module, and Plugin Model

Status: Active design direction  
Owner: NGIN umbrella workspace (`NGIN`)  
Last updated: 2026-03-05

Depends on:

- `001-module-dependency-graph.md`
- `002-runtime-kernel-design.md`

Concrete draft artifacts:

- `docs/api-drafts/NGIN.Core-ApplicationModel.md`
- `manifests/project.schema.json`
- `manifests/package.schema.json`

## Summary

NGIN should treat **packages** as the main distribution unit, with modules and plugins as contents that a package may provide.

This is broader and more useful than designing only a binary plugin ABI in isolation.

## Goals

- define one coherent composition and distribution model
- support source-based and binary-based consumption
- make plugin discovery part of package resolution
- give tooling a stable unit for install, publish, cache, and diagnostics
- support first-party and third-party ecosystems

## Non-Goals

- remote registry protocol details
- package signing and trust policy details
- hot reload implementation details

## Packaging Thesis

A plugin is not enough.

NGIN needs a unit that can distribute:

- headers and libraries
- modules
- dynamic plugin binaries
- config defaults
- assets
- templates
- tooling metadata

That unit should be the **package**.

## Package Structure

Each package should have a manifest describing:

- package name
- package version
- publisher/vendor
- compatible platform range
- compatible ABI or package format version
- provided modules
- provided plugins
- optional package bootstrap hook metadata
- package dependencies
- supported platforms
- optional tools/templates/assets

## Package Bootstrap Convention

The first concrete draft now defines a formal package bootstrap convention so packages can participate in builder setup in a toolable way.

Rules:

- package manifest may declare a `bootstrap` object
- bootstrap `mode` is `BuilderHookV1` in the first draft
- bootstrap `entryPoint` is a unique symbol-style identifier
- bootstrap executes against `PackageBootstrapContext`
- package bootstraps use the same services/modules/plugins/configuration surfaces as the main application builder

This is the NGIN platform equivalent of a package adding itself to the builder in one step.

## Module Rules

Every module should declare:

- identity
- semantic version
- dependency list
- supported host profiles
- supported platforms
- service contributions
- required services
- whether it is editor-only or runtime-safe

Modules may be delivered statically or via packages/plugins.

## Plugin Rules

Plugins should be treated as optional runtime activation units contributed by packages.

Every plugin should declare:

- plugin identity
- plugin version
- compatible platform range
- ABI or host contract version
- contributed modules
- required package/module dependencies
- supported host profiles
- supported platforms

## Discovery Order

Preferred discovery order:

1. package catalog
2. project/application manifest
3. explicit plugin/package search paths
4. direct filesystem probing

This keeps startup deterministic and tooling-friendly.

## Compatibility Checks

Before activating a package or plugin, the host should validate:

- platform version compatibility
- package/plugin format compatibility
- host profile compatibility
- operating system and architecture support
- required modules and services
- version ranges for dependencies

Failures must be visible in diagnostics, not silently ignored.

## Package Cache Direction

NGIN should eventually have a local package cache that supports:

- repeatable local development
- reproducible builds
- offline installs where possible
- package graph inspection

The package cache model should be simple first and distributed later.

## Publish And Install Operations

The platform CLI should eventually support:

- `ngin package add`
- `ngin package remove`
- `ngin package list`
- `ngin package pack`
- `ngin package publish`
- `ngin package restore`

These commands should operate on package manifests, not bespoke per-project scripts.

## Binary Plugin Seam

NGIN still needs a dynamic plugin ABI seam.

That seam should be designed as part of the package model, not as a standalone concept divorced from package metadata.

The ABI seam should provide:

- host/plugin version negotiation
- plugin entrypoint contract
- lifecycle hooks
- error reporting
- module contribution handshake

## Project And Workspace Manifests

Applications and workspaces should declare:

- package references
- enabled modules
- enabled or disabled plugins
- target kind
- host profile
- environment defaults

This gives the CLI and editor a stable project model to understand.

Chosen concrete draft rule:

- `ngin.project.json` is the shared project model for the future CLI and VS Code extension
- `ngin.package.json` is the minimal reusable package manifest
- these files complement the existing module/plugin catalogs during the transition

## Current State

Today the workspace already has:

- module catalogs
- plugin catalogs
- target catalogs
- platform release manifests
- concrete draft project/package schemas

The next step is to unify those ideas under a package-first model instead of treating plugins as the only extension concept.

## Acceptance Criteria

This spec is satisfied when:

- packages are the default way to describe reusable platform content
- plugins become one package capability instead of a parallel unmanaged system
- tooling can install, inspect, and publish platform content predictably
