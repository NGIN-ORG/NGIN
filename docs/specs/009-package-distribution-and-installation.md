# Spec 009: Package Distribution and Installation

Status: Draft
Last updated: 2026-03-08

## Purpose

This spec defines the intended distribution model for NGIN packages.

It separates:

- authored package manifests
- distributable package archives
- installed package store layout

The goal is to support both:

- source-based NGIN usage
- installed or prebuilt NGIN usage

without changing the authored project/package model.

## Core Position

Lock these roles:

- `.nginpkg` is the authored package manifest
- `.nginpack` is the distributable package archive
- installed packages are extracted into a local or global package store

`.nginpkg` should not be overloaded to mean both XML manifest and packaged archive.

## Package Forms

### Authored Package Manifest

File:

- `.nginpkg`

Purpose:

- authoring
- package identity and version
- dependencies
- artifacts
- modules, plugins, and content
- backend-thin build hints

### Package Archive

File:

- `.nginpack`

Purpose:

- single-file distributable package
- signable
- cacheable
- installable

### Installed Package

Location:

- extracted package store entry on disk

Purpose:

- local package consumption by `ngin`
- repeatable build and staging inputs

## Archive Contents

A `.nginpack` archive should contain:

- one `.nginpkg` manifest
- packaged artifact payloads
- packaged content payloads
- metadata
- checksums
- signature metadata

Suggested logical layout:

```text
/
  manifest/
    package.nginpkg
  artifacts/
    lib/
    bin/
  content/
  metadata/
    package-info.xml
    checksums.xml
    signature.xml
```

The physical container format may be zip-based or another simple archive container. The public contract is the logical package structure, not a specific archive library choice.

## Identity

Packages are identified by:

- `Name`
- `Version`

Minimal package key:

- `Name@Version`

## Installed Package Store

Suggested layout:

```text
<store>/
  packages/
    NGIN.Core/
      0.1.0/
        manifest/
          package.nginpkg
        artifacts/
          lib/
          bin/
        content/
        metadata/
```

The same layout should work for:

- a user-local store
- a machine-wide installed store
- a workspace-local cache/store

## Source And Installed Consumption

Projects reference packages by identity, not by delivery mode.

Example:

```xml
<PackageRef Name="NGIN.Core" VersionRange=">=0.1.0 &lt;0.2.0" />
```

In source mode:

- the package may be backed by `Source`
- `ngin build` may build package artifacts from source

In installed mode:

- the package may be backed by `Prebuilt`
- `ngin build` consumes installed artifacts from the package store

The project manifest stays the same in both cases.

## Archive Types

The distribution model must support:

- source-derived first-party packages
- imported third-party packages
- prebuilt runtime libraries
- tool executables
- content-only or plugin-first packages

The package archive format must not assume every package is a source-built library.

## Relationship To Other Files

- `.nginpkg`
  Authored package contract

- `.nginpack`
  Distributable package archive

- `.ngintarget`
  Generated staged application target

These three file types must remain distinct.

## CLI Direction

Future package lifecycle commands should include:

- `ngin package pack`
- `ngin package inspect`
- `ngin package verify`
- `ngin package install`
- `ngin package uninstall`
- `ngin package restore`

Possible later commands:

- `ngin package publish`
- `ngin package search`
- `ngin package pull`

## Signing

The package format should support:

- archive integrity verification
- file checksums
- package signatures
- policy-based trust requirements

Signing policy is separate from the archive format itself.

## First Milestone

The first serious package distribution milestone should define:

1. `.nginpack` archive format
2. package metadata files inside the archive
3. installed package store layout
4. `ngin package pack`
5. `ngin package inspect`
6. `ngin package install`
7. checksum verification
8. signature metadata slots, even if trust policy remains minimal at first

## Non-Goals For The First Distribution Milestone

- remote registry protocol
- package publishing workflow
- feed search or discovery UX
- delta updates
- cross-package deduplicated content store
- full trust infrastructure

## Intent

NGIN should let users work the same way whether they consume the platform from source or from an installed distribution:

- author `.nginproj`
- reference packages by identity
- build through `ngin`

Package distribution should change how packages are delivered, not how projects are authored.
