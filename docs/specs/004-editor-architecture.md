# Spec 004: Editor and Product Architecture

Status: Active design direction  
Owner: NGIN umbrella workspace (`NGIN`)  
Last updated: 2026-03-05

Depends on:

- `001-module-dependency-graph.md`
- `002-runtime-kernel-design.md`
- `003-plugin-abi-header-spec.md`

## Summary

The NGIN editor should be designed as an ordinary NGIN application built on the same host, package, module, and plugin model as every other app.

The editor is important, but it should not distort the platform into becoming editor-first or engine-only.

## Goals

- make the editor a proof that the platform model is real
- keep editor-specific behavior outside the core hosting layer
- support dockable tools, inspectors, and workflows as modules/plugins
- keep runtime and editor products related but separate

## Non-Goals

- concrete UI toolkit choice
- layout serialization format details
- rendering framework details

## Product Position

The editor should be treated as:

- an NGIN application
- using the `Editor` host profile
- composed from packages, modules, and plugins
- capable of consuming runtime/domain metadata without redefining the platform

## Editor Responsibilities

The editor product should provide:

- project/workspace loading
- command system
- panel and tool model
- inspector workflows
- asset and content workflows
- diagnostics and graph inspection
- package and plugin management surfaces

## Editor Composition Model

Editor functionality should be delivered through modules and packages such as:

- `Editor.Workspace`
- `Editor.Layout`
- `Editor.Commands`
- `Editor.Panels`
- `Editor.Inspector`
- `Editor.AssetBrowser`
- `Editor.Diagnostics`

The exact names can change. The important part is that editor features are modular and removable.

## Panel And Tool Contract

Editor panels and tools should be treated as plugin-friendly features.

Each tool contribution should be able to declare:

- identity
- command contributions
- panel contributions
- menu or action contributions
- required services
- compatible host profile and platform version

## Relationship To Runtime Products

The editor may consume metadata and services exported by runtime/domain packages.

The reverse must not be required.

This keeps shipping games and services free from editor-only dependencies.

## Relationship To The Engine

The game engine should be a product family built on NGIN.

That means:

- editor and runtime/player builds share packages where appropriate
- both use the same core host model
- neither forces game-specific assumptions into the core hosting layer

## Project System Direction

The editor should understand the same project model as the CLI.

Projects should declare:

- target type
- host profile
- package references
- enabled modules
- plugin configuration
- build and run profiles

This avoids maintaining separate mental models for CLI and editor workflows.

## Diagnostics Direction

The editor should be able to inspect:

- resolved package graph
- resolved module graph
- plugin load outcomes
- service registrations
- configuration layers

That visibility is critical if NGIN is meant to be the common environment for many project types.

## Acceptance Criteria

This spec is satisfied when:

- the editor starts from the same builder/host model as other NGIN applications
- editor features are contributed through modular packages/plugins
- runtime shipping targets remain independent of editor-only packages
