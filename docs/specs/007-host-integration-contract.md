# Spec 007: Host Integration Contract

Status: Active
Last updated: 2026-03-07

## Purpose

This spec defines the boundary between NGIN tooling and the runtime host.

## Principle

Tooling resolves. The host runs.

The host should consume a resolved target model or staged target artifact. It should not reconstruct authoring intent from unrelated source manifests during normal startup.

## Required Host Inputs

Host startup should receive:

- target identity
- working directory
- environment name
- config sources
- resolved module set
- optional plugin set
- package bootstrap metadata needed for startup

## Mapping To `NGIN.Core`

The resolved target model should map into:

- `KernelHostConfig`
- per-kernel module catalogs
- plugin seams such as `IPluginCatalog` and `IPluginBinaryLoader`
- configuration layering

`NGIN.Core` remains the active host implementation.

## Rules

- host runtime APIs should operate on resolved composition state, not broad workspace metadata
- per-kernel catalogs remain explicit
- runtime startup should not depend on user-facing lockfiles
- staged target artifacts should become the preferred launch input for future `ngin run`

## Near-Term Direction

- align shared model types between CLI and host code
- reduce duplicate manifest parsing between tooling and runtime
- keep runtime discovery seams narrow and implementation-focused
