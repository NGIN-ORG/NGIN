---
title: NGIN Tools for VS Code
description: Navigate and operate NGIN products through the CLI's versioned editor protocol.
---

# NGIN Tools for VS Code

The extension provides product-aware navigation and normal native development
workflows while keeping the `ngin` CLI authoritative.

## Workspace view

The NGIN Activity Bar presents authored NGIN and explicitly registered CMake
projects. Product rows retain physical files while overlaying semantic Build,
Stage, Action, generated, external, and issue information.

## Active project and build context

An explicitly selected Active Project supplies the default for build, tasks,
F5, and Ctrl+F5. The build context includes Configuration, Target, Toolchain,
Profile, and Option choices. Selecting a Run is launch state, not a compiler
dimension.

## Safe authoring

Semantic creation, membership changes, rename, move, and delete request a
versioned plan from the CLI. The extension previews and applies the returned
minimal edits through one VS Code workspace transaction.

Generic New File and New Folder remain physical operations. They do not
silently change Build or Stage membership.

## Operations

Build, Run, Debug, Test, Benchmark, analyzers, and formatters use cancellable
progress, Problems, Output, native tasks, Testing, and the platform C++ debug
adapter. Opening a source file never configures the project merely to populate
the tree.

## Trust boundary

Package-provided tools and build operations require a trusted VS Code workspace.
Untrusted workspaces retain safe physical navigation and non-executing metadata.
