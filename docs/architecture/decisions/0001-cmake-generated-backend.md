# Use CMake as the generated backend

## Status

Accepted

## Context

NGIN needs a portable way to drive native compilers and reuse the CMake package
ecosystem without making handwritten CMake the application model.

## Decision

NGIN resolves manifests into a Composition Graph and generates CMake from that
graph. CMake and the native compiler remain visible implementation layers.

## Consequences

Users can inspect and debug generated builds, and source-backed CMake packages
can join the build. NGIN currently depends on CMake as its only generated
backend, so backend-neutral intent must not be replaced with CMake-specific
authoring unless an explicit compatibility path requires it.
