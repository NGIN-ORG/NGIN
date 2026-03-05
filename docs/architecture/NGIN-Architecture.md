# NGIN Architecture

Status: Active platform architecture  
Owner: NGIN umbrella workspace (`NGIN`)  
Last updated: 2026-03-05

## Purpose

`NGIN` is the top-level workspace and release repository for the NGIN platform.

NGIN is not a single engine library. It is a **general C++ application platform** made up of separate reusable libraries, a hosted application model, and shared tooling/package conventions.

The platform should support:

- games
- editors
- GUI apps
- CLI tools
- services/daemons
- domain-specific engines

## Core Product Idea

The central idea of NGIN is:

1. create an application builder
2. register services
3. add modules and packages
4. load plugins where appropriate
5. build the application host
6. run the application

That builder/host experience is the identity of the platform.

## What Lives In This Repo

This repo exists to define and coordinate the platform:

- architecture and normative specs
- compatibility and release manifests
- workspace tooling
- package/module/plugin metadata schemas
- cross-repo validation and integration rules

This repo should not pretend to be the single SDK library that applications link against.

## Repository Model

NGIN uses a federated/polyrepo model:

- each component repo remains independently buildable and versioned
- the umbrella workspace publishes platform-level compatibility
- tooling and specs live here and describe how the pieces fit together

This is a good fit for NGIN so long as the user experience still feels like one platform.

## Platform Layers

NGIN should be understood as four layers.

### 1. Foundation Libraries

Reusable general-purpose libraries that are valuable even outside the full platform.

Examples:

- `NGIN.Base`
- `NGIN.Log`
- `NGIN.Reflection`

### 2. Core Hosting Layer

The orchestration layer that gives NGIN its identity.

Responsibilities:

- application builder
- host lifecycle
- service registration and resolution
- configuration
- module loading
- plugin loading
- package integration
- events and tasks
- environment and host profiles

Current implementation note:

- today this functionality lives in `NGIN.Core`
- target long-term product naming is `NGIN.Core`

### 3. Product and Domain Packages

Optional first-party or third-party packages built on the hosting layer.

Examples:

- `NGIN.ECS`
- editor framework
- rendering
- assets/content pipeline
- diagnostics
- scripting

### 4. Applications

Final deliverables built by composing the layers above.

Examples:

- editor applications
- games
- calculators
- CLI tools
- backend services

## Naming Direction

The current name `NGIN.Core` is serviceable for the existing implementation, but it does not best express the approved platform direction.

Recommended naming direction:

- umbrella platform: `NGIN`
- reusable libraries: `NGIN.Base`, `NGIN.Log`, `NGIN.Reflection`, ...
- central hosting library: `NGIN.Core`

Optional stricter split if needed later:

- `NGIN.Core`: contracts and orchestration primitives
- `NGIN.Hosting`: default builder/host implementation

## Platform Principles

- Domain-agnostic: no hard dependency on game-specific concepts.
- Builder-first: application startup should feel consistent across all project types.
- Module-driven: composition should happen through modules and packages, not custom bootstrap code.
- Plugin-ready: optional runtime extensibility should be a built-in platform concern.
- Tooling-first: CLI, templates, package operations, and editor integration are part of the product.
- Inspectable startup: app composition, dependency ordering, and configuration should be easy to inspect and validate.

## Game Engine and Editor Position

The game engine and editor fit NGIN well, but they should be treated as products built on NGIN, not as the definition of NGIN.

This keeps the platform honest:

- editors remain ordinary NGIN applications with editor-oriented host profiles
- games remain ordinary NGIN applications with runtime-oriented host profiles
- other apps can reuse the same platform without inheriting engine-specific assumptions

## Release Model

The umbrella workspace owns the platform release version.

Component repos keep their own versions, while `manifests/platform-release.json` defines the compatible set of component refs and roles for a platform release.

## Spec Map

- `001`: platform layers and dependency model
- `002`: application host and builder model
- `003`: package, module, and plugin model
- `004`: editor and product architecture
- `005`: platform transition and next steps

## Concrete Drafts

The first concrete public draft for the application model lives in:

- `docs/api-drafts/NGIN.Core-ApplicationModel.md`
- `docs/api-drafts/include/NGIN/Core/Application.hpp`
- `manifests/project.schema.json`
- `manifests/package.schema.json`

These artifacts use `NGIN.Core` naming only and define the first shared project/package model for future CLI and VS Code integration.

## Near-Term Direction

1. lock platform language around `NGIN` as an application platform
2. define the builder/host contract as the central API surface
3. define packages as the main distribution unit
4. complete the plugin seam inside the package model
5. build the editor and engine/runtime products as proof that the platform is truly general-purpose
