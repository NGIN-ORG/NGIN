# Spec 005: Platform Transition and Next Steps

Status: Active roadmap  
Owner: NGIN umbrella workspace (`NGIN`)  
Last updated: 2026-03-05

Depends on:

- `001-module-dependency-graph.md`
- `002-runtime-kernel-design.md`
- `003-plugin-abi-header-spec.md`
- `004-editor-architecture.md`

## Intent

This roadmap assumes breaking changes are acceptable when they move NGIN toward the approved platform direction:

- NGIN as a general C++ application platform
- builder-first application startup
- package/module/plugin composition
- editor and engine as products built on the platform

## Phase 1: Lock Product Language

Goal:

- stop mixing platform, runtime, host, and core terminology

Actions:

- adopt `NGIN` as the umbrella platform name
- adopt `NGIN.Core` as the target name for the central hosting library
- treat `NGIN.Runtime` as the temporary implementation name until the rename is executed
- standardize terms: package, module, plugin, target, host profile

Exit criteria:

- active docs use one coherent vocabulary
- manifests and tooling have an explicit transition plan for renamed concepts

## Phase 2: Define The Builder And Host Contract

Goal:

- make the host model the center of the platform

Actions:

- define `ApplicationBuilder`
- define `ApplicationHost`
- define service/module/package/plugin registration flows
- define host profiles and lifecycle states
- define startup diagnostics expectations

Exit criteria:

- at least one sample app can start entirely from the builder model
- startup composition can be inspected before activation

## Phase 3: Move To A Package-First Model

Goal:

- make packages the main unit of distribution

Actions:

- define package manifest format
- define package references in project/app metadata
- connect module and plugin metadata to packages
- define package cache layout and restore workflow

Exit criteria:

- a target can declare package references and resolve modules/plugins through them
- package compatibility becomes part of normal validation

## Phase 4: Complete Dynamic Plugin Support

Goal:

- turn the current plugin seam into a usable product feature

Actions:

- finish dynamic plugin loader contract
- implement compatibility negotiation
- add manifest-first plugin discovery
- report plugin rejection causes clearly

Exit criteria:

- a package can contribute a dynamic plugin that is discovered and validated by the host
- failures are deterministic and well reported

## Phase 5: Build The CLI Product

Goal:

- make NGIN pleasant to use across all project types

Actions:

- define commands such as `ngin new`, `ngin build`, `ngin run`, `ngin test`, `ngin package`, `ngin doctor`, `ngin graph`
- make the CLI understand projects, packages, host profiles, and targets
- replace ad hoc workflows where possible with stable CLI entrypoints

Exit criteria:

- new projects can be scaffolded from templates
- package and graph operations are available from one CLI

## Phase 6: Build First-Party Proof Products

Goal:

- validate that the platform is genuinely general-purpose

Actions:

- build an editor application on the NGIN host model
- build a game/runtime product on the same host model
- build at least one non-engine application such as a CLI tool or calculator

Exit criteria:

- all three product categories share one platform startup model
- editor-only assumptions are not required by non-editor apps

## Phase 7: Build VS Code Integration

Goal:

- make NGIN feel like one operating environment for development

Actions:

- scaffold projects from templates
- add package/module/plugin references from the extension
- launch targets and inspect graphs
- surface package and host diagnostics inside the editor

Exit criteria:

- VS Code understands NGIN concepts instead of acting as a thin build button

## Immediate Next Actions

These are the concrete next moves I would recommend now.

1. Rename the conceptual platform language from `Runtime` to `Core` across docs and planning.
2. Write the first concrete `ApplicationBuilder` API draft.
3. Design the package manifest before going deeper on standalone plugin ABI details.
4. Define a minimal NGIN project manifest that both the CLI and future editor can consume.
5. Add one end-to-end example app that proves the builder/package/module flow.

## Risks

1. Reusing old runtime terminology will keep producing design drift.
2. Building plugin ABI details before the package model will create a narrow extension system.
3. Letting the editor define the platform too early will leak tool-specific assumptions into core hosting.
4. Letting the game engine define the platform too early will make the platform less useful for everything else.

## Success Criteria

NGIN is on the right path when:

- creating a game, editor, CLI tool, and service feels like the same platform experience
- the central library is understood as the application host/core layer
- packages, modules, and plugins form one coherent composition story
- tooling and IDE integration operate on platform concepts, not repo-specific scripts
