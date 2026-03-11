# Spec 010: Workspace And Project Model

Status: Draft
Last updated: 2026-03-10

## Purpose

This spec defines the intended split between:

- workspace files
- project files
- package files

The goal is to make NGIN application authoring feel project-first, while keeping packages as the reusable and distributable unit.

This is a staged architecture change. It is not a vocabulary-only rename of the current `.nginproj` model.

## Problem Statement

The current manifest model has two active problems:

1. `.nginproj` is currently a composition and target manifest, not a real build project.
2. normal applications currently feel package-owned, because executable artifacts, runtime modules, and build metadata are still package-centered.

This creates visible friction in examples such as `App.Basic`, where the application is only buildable because an app package owns the executable artifact and runtime contribution.

It also creates semantic drift in the umbrella repo, where the old `NGIN.Workspace.nginproj` sample had already started behaving more like a workspace or solution file than a single application project.

## Decision

NGIN should adopt three distinct authored file roles:

- `.ngin`
- `.nginproj`
- `.nginpkg`

The intended meaning is:

- `.ngin`
  workspace file
- `.nginproj`
  project file
- `.nginpkg`
  package file

## File Roles

### Workspace File

Extension:

- `.ngin`

Purpose:

- define a workspace containing one or more projects
- define workspace-level settings and discovery roots
- act as the developer-facing equivalent of a solution/container

The workspace file is the workspace authority. It replaces the old surrogate workspace project and the legacy release/catalog split that existed earlier in the repo.

### Project File

Extension:

- `.nginproj`

Purpose:

- define the authored application, library, tool, plugin, or other buildable unit
- own source roots and build outputs
- reference packages and other projects
- define variants and launch selection

Projects are the primary authored unit for application development.

The main application entrypoint should normally belong to a project, not to a package.

### Package File

Extension:

- `.nginpkg`

Purpose:

- define a reusable or distributable unit
- define reusable artifacts, modules, plugins, and content
- define package dependency and package distribution metadata

Packages remain the primary unit of reuse and distribution.

Packages should not be the default home of a normal application's `main.cpp`.

## Conceptual Model

Recommended mental model:

- workspace = organization and developer workflow
- project = the thing being built
- package = the thing being reused or distributed

In plain terms:

- users author applications as projects
- projects depend on packages
- packages provide reusable capabilities and distribution units

## Project Model

The future `.nginproj` model should become project-shaped at the root.

At minimum, a project should define:

- `Name`
- `Type`
- `SourceRoots`
- `PrimaryOutput`
- `ProjectRefs`
- `PackageRefs`
- `ConfigSources`
- `Variants`

### Name

Human-readable and stable project identity inside a workspace.

### Type

Suggested initial types:

- `Application`
- `Library`
- `Tool`
- `Plugin`

This is a project concern, not a package concern.

### SourceRoots

Defines where project-owned source files live.

This is one of the key fields missing from the current `.nginproj` model.

### PrimaryOutput

Defines the main output produced by the project.

Suggested initial output kinds:

- `Executable`
- `StaticLibrary`
- `SharedLibrary`

Version 1 should require one primary output, even if future projects may support additional outputs.

### ProjectRefs

Defines source/build graph references to other projects in the same workspace.

These are not package references.

### PackageRefs

Defines reusable or distributed dependencies consumed by the project.

These remain package-based and should continue to use canonical package identities.

### ConfigSources

Defines configuration files or configuration roots associated with the project or project variant.

### Variants

Defines platform/profile/environment or other build/run variants.

Variants should hold overrides and selection data, not be used to model unrelated applications.

Examples:

- `Debug`
- `Release`
- `Editor`
- `Runtime`
- `DedicatedServer`

## Package Model

The current package direction remains valid.

Packages should continue to own:

- package identity
- package dependencies
- reusable artifacts
- reusable modules
- reusable plugins
- reusable staged content
- package distribution metadata

Packages are still expected to provide runtime contributions in the normal reusable case.

What changes is the authored application boundary:

- the app itself should be a project
- the app should consume packages
- the app may later emit a package

## Project References vs Package References

These must be treated as different resolution modes.

### ProjectRef

Project references are:

- source/build graph edges inside a workspace
- local development-time relationships
- not distribution units

### PackageRef

Package references are:

- reusable or distributed dependency edges
- package-resolution inputs
- the normal path for consuming platform and external functionality

Rules:

- projects may reference projects
- projects may reference packages
- packages may reference packages
- packages must not reference projects

## Runtime Contributions

This is a major open implementation gap in the current system.

Today, modules and plugins are package-owned.

If applications stop being authored as packages, projects need a first-class way to declare app-local runtime contributions.

The future model must answer:

- how a project declares its app entry module
- how a project declares project-owned executable artifacts
- whether project-local plugins exist in v1

Version 1 should support at least:

- one project-owned executable output
- one project-owned runtime contribution path

without forcing application entrypoints through `.nginpkg`.

## Launch Selection

Launch selection remains necessary.

Projects should allow explicit runnable output selection when more than one executable is visible.

Rules:

- if exactly one executable is available, implementations may infer it
- if multiple executable candidates are available, explicit selection should be required
- if no executable is available, the project may still be valid for validation or library outputs

## Workspace Discovery

`.ngin` is the workspace authority in the current implementation.

Package resolution comes from package source roots declared in the workspace file.

Therefore:

- single-project, no-workspace mode is not an active contract in v1
- the workspace file must remain the root for package discovery

## Migration Plan

This change should be implemented in stages.

### Stage 1

Introduce `.ngin` as an explicit workspace file and use it to replace the current semantic overloading of the old workspace-shaped `.nginproj` sample.

### Stage 2

Expand `.nginproj` into a real project model with:

- source roots
- primary output
- project references
- package references
- variants

### Stage 3

Define project-local runtime contributions so applications can own their entry module and executable without being packaged first.

### Stage 4

Extract shared manifest and composition code used by both:

- `ngin`
- `NGIN.Core`

This should happen before broad rollout of the expanded schema.

### Stage 5

Migrate examples such as `App.Basic` from package-owned application layout to project-owned application layout.

## Recommended Answers

The following positions are recommended for the first serious implementation pass.

### 1. Should `.ngin` be required in version 1, or only required for multi-project workspaces?

Recommended answer:

- require `.ngin` in version 1 for the new workspace/project model

Rationale:

- the current implementation is still workspace-root driven
- package discovery and project discovery are still tied to workspace-local metadata
- pretending `.ngin` is optional in v1 would create a misleading contract

Relaxing `.ngin` to optional can be considered later, but only after package discovery and project discovery have a real single-project mode that does not depend on workspace-local catalogs.

### 2. Should `.nginproj` support explicit file lists, source roots, or both?

Recommended answer:

- support source roots in version 1
- do not require explicit file lists in version 1
- allow later opt-in exclusions or explicit file groups if needed

Rationale:

- source roots keep the project model small and readable
- explicit file lists are noisy and fragile for native projects
- this keeps the project contract closer to intent than to build-system bookkeeping

### 3. Where should project-local runtime module declarations live?

Recommended answer:

- project-local runtime contributions should live directly in `.nginproj`

Suggested direction:

- add a project-owned runtime section under the project model
- this section should declare at least:
  - project-local modules
  - project-owned executable/runtime binding

Rationale:

- if the application is project-owned, its entry module should not have to be re-expressed as a package just to exist
- package-owned runtime contributions remain correct for reusable units, but app-local runtime contributions need a first-class project path

### 4. Should projects be allowed to declare plugins directly in version 1, or only modules and executables?

Recommended answer:

- version 1 should allow project-owned modules and executables
- version 1 should not require direct project-owned plugin declarations

Rationale:

- plugins are a secondary concern compared to the app entry module and primary executable
- keeping project-local plugins out of v1 prevents the project model from becoming too wide too early
- reusable plugin behavior remains package-owned in the normal path

### 5. Can a project produce multiple outputs in version 1, or should one primary output be required?

Recommended answer:

- require one primary output in version 1

Allow later:

- additional outputs
- support executables
- project-owned tools

Rationale:

- the current staging and launch model is centered on one selected executable plus supporting artifacts
- requiring one primary output keeps build, stage, and run semantics clear

### 6. Should a project be packable directly into one or more `.nginpkg` / `.nginpack` outputs?

Recommended answer:

- yes, but not as an implicit behavior in version 1

Suggested direction:

- a project may declare package outputs or package emission settings later
- packaging should be an explicit project capability, not an automatic assumption

Rationale:

- many apps are not primarily reusable packages
- some libraries and plugins should clearly be packable
- keeping packaging explicit avoids blurring “build an app” with “publish a package”

### 7. How should project-level launch selection interact with package-provided executable artifacts?

Recommended answer:

- project-owned executable outputs should be preferred as the normal runnable output
- package-provided executable artifacts should usually be treated as tools, support executables, or explicit alternate launch candidates

Rules:

- if exactly one project-owned executable exists, it is the default runnable output
- if multiple runnable executable candidates exist, explicit launch selection is required
- package-provided executables should not silently displace a project-owned app executable

Rationale:

- this preserves the intuition that the project is the thing being built
- packages may still provide tools or alternate executables without taking ownership of the app boundary

### 8. Should `.ngin` replace earlier workspace release/catalog metadata, or point to it as migration input?

Recommended answer:

- `.ngin` should become the authority
- older workspace release/catalog inputs should be removed rather than kept as parallel authorities

Rationale:

- NGIN should not keep multiple authorities for workspace state
- the repo is still early enough that a hard cutover is cheaper than carrying migration debt

### 9. How should package resolution work for a single-project app if workspace-local catalogs are eventually removed?

Recommended answer:

- long term, package resolution should come from declared package sources, not from an implicit workspace catalog

Suggested sources in priority order:

- project-local package roots
- workspace-declared package roots
- installed package stores
- configured package feeds later

For version 1:

- require `.ngin`
- let `.ngin` provide the package discovery roots

Rationale:

- this preserves a clean path toward installed/distributed NGIN usage
- it avoids baking the current local catalog mechanism into the permanent product model

### 10. How much of the current CLI artifact-resolution logic should move into the shared manifest/composition library before the new project model becomes active?

Recommended answer:

- enough to make project, package, target, and artifact interpretation shared between `ngin` and `NGIN.Core`

At minimum, the shared library should own:

- workspace parsing
- project parsing
- package parsing
- normalized project/package model types
- package and project reference resolution
- artifact selection rules
- launch selection rules

The CLI may continue to own:

- command dispatch
- user-facing output
- backend invocation
- staging orchestration

Rationale:

- the current CLI/runtime manifest drift is already visible
- introducing `.ngin` plus an expanded `.nginproj` without shared interpretation would multiply that drift

## Remaining Open Questions

The following areas remain intentionally open:

1. Final `.ngin` XML schema shape.
2. Exact syntax for project-local runtime contribution sections.
3. Whether project-local plugins ever become a first-class authored concept.
4. How project package-emission should be declared when packaging is enabled.
5. Whether explicit source exclusions or source groups are needed in version 1.

## Non-Goals For This Spec

This spec does not define:

- final `.ngin` XML schema
- final project build backend schema
- final package distribution workflow
- remote package feeds or registries
- final plugin distribution story

Those remain separate concerns.

## Quality Bar

This split should make NGIN:

- clearer for normal application authors
- more explicit about the difference between authored apps and reusable packages
- easier to scale from one app to multi-project workspaces
- more honest about the current package-centered implementation constraints

It should not introduce a second layer of ambiguous authority or a shallow rename of the current manifest model.
