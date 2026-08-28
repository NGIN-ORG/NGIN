# Keep project tooling separate from application runtime

## Status

Accepted

## Context

Many C++ projects need a consistent build and staging workflow but do not want
an application framework linked into the product.

## Decision

The `ngin` CLI and manifest model are build-time tooling. `NGIN.Core` is an
optional C++ library/framework selected like any other dependency.

Manifests describe what must be acquired, built, linked, generated, staged,
published, run, tested, and benchmarked. Application source and its chosen runtime framework
describe dependency injection, services, runtime modules, backend selection,
startup/shutdown ordering, and application behavior.

The manifest grammar has no Host, runtime Module, runtime-service, or
application lifecycle model. A dynamically loaded plugin is visible to the CLI
only as a product/export and deployable artifact. The runtime decides whether,
when, and how to load it.

## Consequences

A plain C++ executable can use the complete project workflow without an NGIN
runtime dependency. Applications that choose `NGIN.Core` compose its services,
modules, configuration, and lifecycle in C++. Manifest and runtime graphs do
not duplicate each other.
