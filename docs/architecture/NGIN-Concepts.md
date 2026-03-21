# NGIN Concepts

## Project

The buildable authored unit.

## Configuration

One named setup of a project. A configuration narrows build and launch selection for the same project. It is not the place to model unrelated executables.

## Package

The reusable unit. Packages expose artifacts, dependencies, modules, plugins, content, and bootstrap metadata.

## Workspace

An optional multi-project root that provides project discovery, package sources, and package providers.

## Launch Manifest

`.nginlaunch` is generated, not authored. It is the handoff from CLI build/staging to runtime launch/debug flows.
