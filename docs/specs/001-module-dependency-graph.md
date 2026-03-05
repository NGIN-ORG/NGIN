# Spec 001: Platform Layers and Dependency Model

Status: Active normative spec  
Owner: NGIN umbrella workspace (`NGIN`)  
Last updated: 2026-03-05

## Summary

This spec defines the main nouns and layering rules for NGIN:

- repositories/components
- packages
- modules
- plugins
- targets
- host profiles

Its job is to keep NGIN coherent as a general application platform instead of letting each product invent its own boot and dependency model.

## Goals

- keep `NGIN.Base`, `NGIN.Log`, and similar libraries broadly reusable
- define clear downward-only dependency rules
- support both static composition and runtime plugin loading
- ensure editors, games, tools, and services can share one platform model
- create a stable vocabulary for tooling, manifests, and future code APIs

## Non-Goals

- builder API details and host lifecycle semantics
- binary plugin ABI function tables
- editor UI behavior and workspace layout details

## Terminology

- **Repository**: a Git repository such as `NGIN.Base`
- **Component**: a published repo/library unit tracked by the platform release manifest
- **Package**: a distributable unit that may contain modules, plugin binaries, assets, metadata, templates, and tooling data
- **Module**: a composable feature unit registered into an application host
- **Plugin**: an optional runtime extension delivered through a package and activated by the host
- **Target**: a final build product such as an editor, game, CLI tool, or service
- **Host profile**: a named application mode with platform defaults, such as `Editor` or `Service`

## Platform Layers

### Layer 1: Foundation

Purpose:

- low-level reusable systems
- no dependency on higher-level hosting or product concepts

Examples:

- `NGIN.Base`
- `NGIN.Log`
- `NGIN.Reflection`

### Layer 2: Core Hosting

Purpose:

- application builder
- host lifecycle
- services
- config
- event/task contracts
- module and plugin activation
- package integration

Current implementation note:

- current code lives in `NGIN.Runtime`
- target product naming is `NGIN.Core`

### Layer 3: Product and Domain Packages

Purpose:

- reusable platform products and domain features

Examples:

- ECS
- editor framework
- rendering
- assets
- diagnostics
- scripting

### Layer 4: Applications

Purpose:

- the final user-facing executable or service

Examples:

- game
- editor
- calculator
- CLI tool
- daemon/service

## Dependency Direction

Dependencies may point:

- downward across layers
- within the same layer only when the relationship is explicitly supported

Dependencies may not point upward.

## Forbidden Examples

- foundation depending on core hosting
- core hosting depending on editor-only modules
- reusable domain packages depending on a single final application
- runtime shipping products depending on editor-only packages

## Allowed Dependency Matrix

Legend:

- `Y` allowed
- `O` optional/conditional
- `N` forbidden

| From \\ To   | Foundation | Core Hosting | Product/Domain | Editor Product | Application |
|--------------|------------|--------------|----------------|----------------|-------------|
| Foundation   | Y          | N            | N              | N              | N           |
| Core Hosting | Y          | Y            | N              | N              | N           |
| Product/Domain | Y        | Y            | Y              | N              | N           |
| Editor Product | Y        | Y            | O              | Y              | N           |
| Application  | Y          | Y            | Y              | O              | Y           |

Notes:

- editor-facing packages may depend on neutral product/domain packages
- application-to-editor dependencies are optional and only valid for editor targets

## Composition Model

NGIN should support three levels of composition.

### 1. Static modules

Modules compiled directly into the target.

Use when:

- functionality is core to the product
- startup determinism matters more than runtime extensibility
- the module is internal to one app or one tightly coupled product family

### 2. Dynamic plugins

Optional runtime-loaded extensions.

Use when:

- the feature is optional
- the feature should be installable or removable independently
- a third-party extension model is desired
- editor tooling and diagnostics need runtime discovery

### 3. Packages

The distribution boundary for the platform.

A package may include:

- module descriptors
- plugin binaries
- static library metadata
- config defaults
- templates
- assets
- CLI integration metadata

Packages are broader than plugins and should become the main unit of distribution and tooling.

## Host Profiles

The platform should standardize host profiles.

Initial host profiles:

- `ConsoleApp`
- `GuiApp`
- `Game`
- `Editor`
- `Service`
- `TestHost`

Profiles affect defaults, not architecture.

Examples:

- `Editor` enables diagnostics, reflection, and tool/plugin discovery by default
- `Game` uses lean runtime defaults
- `Service` disables UI-oriented facilities
- `TestHost` emphasizes deterministic startup and teardown

## Naming Rules

### Repositories and Components

- Pattern: `NGIN.<Component>`
- Examples: `NGIN.Base`, `NGIN.Log`, `NGIN.Core`

### Modules

- Pattern: `<Family>.<Name>`
- Examples: `Core.Hosting`, `Core.Configuration`, `Editor.Workspace`, `Domain.ECS`

Transition note:

- current manifests still use some `Runtime.*` naming
- target naming should move toward `Core.*` for the central hosting layer

### Packages

- Pattern: `<Vendor>.<Package>`
- Examples: `NGIN.Diagnostics`, `NGIN.Editor`, `Acme.SimExporter`

### Targets

- Examples: `NGIN.EditorApp`, `MyGame`, `MyCalculator`, `MyCliTool`

## Rules For Editor Separation

- editor-only packages must never be required by runtime shipping targets
- runtime products may expose metadata/services consumed by editors
- neutral shared code must live outside editor-specific packages

## Rules For Platform Core

- the core hosting layer must remain domain-agnostic
- rendering, gameplay, editor UI, and asset-import assumptions must not leak into the core hosting layer
- reflection may be optional at runtime, but the platform must support hosts where reflection is enabled by default

## Manifest And Tooling Implications

Workspace tooling should validate:

- layer violations
- cycles
- target/profile validity
- package compatibility
- platform version compatibility
- deterministic target resolution

This spec is the foundation for all later build, package, plugin, and host behavior.
