# Keep backend bindings outside the Composition Graph

## Status

Accepted

## Context

NGIN currently implements generated CMake and CMake-backed package integration.
Concepts such as `find_package`, CMake cache variables, target names, and
toolchain files are not build-system-neutral. Putting them in package exports
or graph nodes would make a future backend require a manifest and graph
redesign.

## Decision

Project and package semantics are backend-neutral. A registered XML namespace
describes backend integration. This overhaul implements only the CMake
namespace and adapter.

Resolution produces two immutable results:

- the Composition Graph, containing only semantic products, PackageInstances,
  active exports, edges, options, capability bindings, actions, deployable
  artifacts, and provenance;
- `CMakeIntegrationBindings`, mapping semantic product/export/tool identities
  to CMake generation, discovery, source-build, and target details.

BuildPlan derivation consumes the graph plus CMakeIntegrationBindings. No
command rereads manifest XML. CMake names and opaque backend data never enter
the graph.

## Consequences

CMake remains the sole implemented and fully supported backend. Unsupported
backend names fail explicitly. Future Meson, Bazel, or other adapters may use
their own namespace and binding set without changing ordinary project
dependencies, package export identity, or graph serialization.
