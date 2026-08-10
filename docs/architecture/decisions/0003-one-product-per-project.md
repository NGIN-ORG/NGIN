# Describe one primary product per project

## Status

Accepted

## Context

Projects that mix several unrelated targets make identity, profiles,
dependencies, launch behavior, staging, and publishing ambiguous.

## Decision

One `.nginproj` describes one primary product: an application, library, tool,
test, benchmark, plugin, or external product. A workspace groups related
projects.

There is no generic Module product. Statically linked reusable code is a
Library, dynamically loaded code is a Plugin, and C++ language modules are
typed build items within a product.

## Consequences

Every resolved graph has one clear product identity. Cross-product relationships
are explicit project references. Repositories with many binaries use more
project manifests, but each manifest stays focused and independently
inspectable.
