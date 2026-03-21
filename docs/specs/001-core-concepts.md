# Spec 001: Core Concepts and Vocabulary

Status: Active
Last updated: 2026-03-21

## Purpose

This spec defines the active V2 vocabulary used by all other active specs.

## Core Terms

### Project

The primary authored buildable unit. Projects are authored in `.nginproj`.

### Configuration

One named setup of a project. A configuration may select build configuration, host profile, platform, environment, working directory, launch executable, and narrow reference or runtime overrides.

### Package

The reusable unit. Packages are authored in `.nginpkg`.

### Workspace

An optional authored repo-level container. Workspaces are authored in `.ngin`.

### Launch Manifest

Generated build output that represents the staged launchable result of a selected project configuration. Launch manifests are written as `.nginlaunch`.

## Modeling Rule

- Different executables or entrypoints should usually be different projects.
- Configurations should represent narrow selection or override data on the same project.
