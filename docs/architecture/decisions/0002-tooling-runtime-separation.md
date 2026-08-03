# Keep project tooling separate from application runtime

## Status

Accepted

## Context

Many C++ projects need a consistent build and staging workflow but do not want
an application framework linked into the product.

## Decision

The `ngin` CLI and manifest model are build-time tooling. `NGIN.Core` is an
optional package selected like any other dependency.

## Consequences

A plain C++ executable can use the complete project workflow without an NGIN
runtime dependency. Applications that choose `NGIN.Core` gain services,
modules, configuration, and lifecycle management, but accept that additional
runtime contract explicitly.
