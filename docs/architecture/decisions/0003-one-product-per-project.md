# Describe one primary product per project

## Status

Accepted

## Context

Projects that mix several unrelated targets make identity, profiles,
dependencies, run behavior, staging, and publishing ambiguous. Treating
Application, Tool, Test, Benchmark, and Plugin as peer artifact kinds also
mixes physical output with consumption or execution roles.

## Decision

One `.nginproj` describes one physical product with an `Executable` or
`Library` root. A workspace groups related projects.

Test and Benchmark are registrations attached to an Executable. Tool is a
package export role for an executable. A dynamically loaded plugin is a
`Library Kind="Plugin"` and may be exposed through a package Plugin export.
None of those roles changes the physical product kind.

Libraries select `Kind="Static"`, `Shared`, `Interface`, or `Plugin`.
Executables have an implicit Run and may declare customized Runs. There is no
generic Module or External project product. C++ language modules remain typed
`CxxModule` build items, while external build integration belongs to packages
and providers.

## Consequences

Every resolved graph records `artifactKind` and, for libraries, `libraryKind`.
Cross-product relationships are explicit project requirements. Repositories
with many binaries use more project manifests, but each stays independently
inspectable. Runtime module registration remains application-owned and does
not enter manifests or the Composition Graph.
