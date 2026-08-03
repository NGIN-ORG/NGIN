# Existing CMake projects

NGIN currently uses CMake as its generated backend, but an NGIN project is
authored in `.nginproj`, not by importing a handwritten project
`CMakeLists.txt`.

There are two common starting points.

## Move an application to NGIN

Create one `.nginproj` for each primary product and describe its sources,
dependencies, staging, and launch behavior there. Keep any repository-level
CMake automation until its responsibility has moved or is no longer needed.

Start with [Your first project](first-project.md), then add the existing source
paths under the product's `<Build>` section.

## Consume an existing CMake dependency

Wrap the dependency with a `.nginpkg` and select a CMake integration mode:

- `AddSubdirectory` for source that can join the generated build.
- `FindPackage` for an installed CMake package.
- `Manual` when the wrapper supplies its integration explicitly.

Workspace `PackageProvider` entries map package identities to source roots.
See [Packages](../guides/packages.md) and the wrappers under
[`Packages/`](../../Packages) for working examples.

NGIN does not currently promise automatic conversion of arbitrary CMake
projects. Migrate one product boundary at a time and keep the generated CMake
visible while checking the result.
