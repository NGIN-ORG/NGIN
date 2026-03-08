# NGIN Architecture

Status: Active platform architecture  
Owner: NGIN umbrella workspace (`NGIN`)  
Last updated: 2026-03-07

## Purpose

NGIN is a C++ application platform built around a common host model.

The platform is not defined by a game engine, editor, or plugin ABI in isolation. It is defined by one composition flow:

1. load a project
2. select a target
3. resolve packages
4. derive modules and plugins from those packages
5. build a host
6. stage or run the result

## Main Principles

- packages are the main authored composition unit
- modules and plugins are provided by packages
- targets are small explicit app variants
- authored manifests are XML, not JSON
- active tooling is native C++, not Python
- lockfiles are not part of the public platform model at this stage
- staged targets are the bridge between tooling and runtime

## Repository Role

This repo owns:

- active specs and architecture
- workspace metadata catalogs and package wrappers
- the native `ngin` CLI
- cross-repo validation and staging

The current host implementation lives in `NGIN.Core`.

The repo is structured intentionally:

- `Packages/` exposes the NGIN-facing package contract
- `Dependencies/` holds source trees that are integrated here but not owned here
- `NGIN.Core/` remains first-class local platform code
- `Tools/NGIN.CLI/` owns the public CLI implementation

## Public File Types

- authored project: `.nginproj`
- authored package: `.nginpkg`
- generated staged target: `.ngintarget`

Lower-level runtime descriptor files are implementation details, not the intended primary authoring surface.

## Current Workflow

1. author project and package manifests
2. validate target composition
3. inspect the resolved graph
4. build a staged target layout through the package wrappers and CMake backend

## Near-Term Direction

1. keep the authored model centered on project, target, package, module, plugin, and host
2. keep packages as the main reusable unit
3. move runtime module and plugin declaration fully into packages
4. make staged output the bridge to future run/build integration
5. build proof products on top of the same host model
