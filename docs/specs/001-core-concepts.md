# Spec 001: Core Concepts and Vocabulary

Status: Active
Last updated: 2026-04-23

## Purpose

This spec defines the active V2 vocabulary used by all other active specs.

## Core Terms

### Project

The primary authored buildable unit. Projects are authored in `.nginproj`.

### Configuration

One named setup of a project. A configuration may select build configuration, operating system, architecture, environment, launch metadata, and narrow reference or runtime overrides.

### Composition

The resolved runtime shape produced from one selected project configuration, optional workspace context, and all resolved project and package contributions. Composition determines what is launchable, what is staged, and what the host/runtime sees.

### Package

The reusable unit. Packages are authored in `.nginpkg`.

### Workspace

An optional authored repo-level container. Workspaces are authored in `.ngin`.

### Launch Manifest

Generated build output that represents the staged, launchable serialization of a resolved composition. Launch manifests are written as `.nginlaunch`.

## Modeling Rule

- Different executables or entrypoints should usually be different projects.
- Configurations should represent narrow selection or override data on the same project.
