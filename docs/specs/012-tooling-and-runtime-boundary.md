# Spec 012: Tooling and Runtime Boundary

Status: Active
Last updated: 2026-04-24

## Purpose

This spec defines the boundary between NGIN tooling and the optional
`NGIN.Core` runtime host.

## Layer Model

NGIN has two separable layers:

- NGIN tooling: workspace, project, package, configuration, build generation,
  staging, validation, graphing, run, debug, and package workflows.
- `NGIN.Core`: an optional C++ application host for services, configuration,
  modules, plugins, lifecycle, reflection, and diagnostics.

The tooling layer must be able to build and stage plain native C++ applications
that do not link `NGIN.Core`.

Applications link `NGIN.Core` only when they want the hosted application model.

## Runtime Independence

Authored manifests are build and composition inputs. A shipped application must
not be required to load `.ngin`, `.nginproj`, or `.nginpkg` files from the source
tree.

A shipped application is a normal native layout:

- executable
- required shared libraries
- assets, content, plugins, and config files
- optional runtime metadata that the application explicitly chooses to consume

## Launch Manifest Role

`.nginlaunch` is a generated tooling artifact. It exists for local run, debug,
inspection, editor integration, and smoke tests.

It may be included in staged developer output, but it is not the required
production runtime contract for shipped applications.

If a runtime needs metadata beyond staged config/content, that metadata should
be defined as an explicit runtime contract instead of overloading `.nginlaunch`.

## `NGIN.Core` Authoring Guidance

Hosted applications should prefer code-first startup plus staged config/content.

`NGIN.Core` may keep project-file loading APIs for development, tests, migration,
or advanced tooling scenarios, but examples and templates should not teach source
project manifests as the normal shipping-time runtime dependency.

