# Resolve package instances and activate named exports

## Status

Accepted

## Context

A package name and version do not identify one usable C++ artifact. The same
release may be acquired separately for the build host and target, or with
different target, toolchain, linkage, configuration, and artifact-affecting
option inputs. Packages may also expose several independent libraries, tools,
plugins, actions, or assets.

Treating package presence as activation of every export stages and links more
than the consumer requested. Treating every realization as the same package
loses the identity needed for cross compilation and binary compatibility.

## Decision

The resolver distinguishes:

- `PackageCoordinate`: NGIN package name plus exact package version;
- `PackageProviderResult`: normalized acquisition result, provider-native
  coordinate/revision, integrity, context, compatibility inputs, and
  provenance;
- `PackageInstance`: one coordinate/provider result in a host or target
  context, with derived binary compatibility and artifact-affecting options.

Exports activate on a PackageInstance. A direct dependency activates its named
exports or the package's unambiguous default export set. Requirements, actions,
plugins, assets, and capability bindings extend the activation closure.

Package-level obligations apply when any export is active. Export-level
requirements and runtime artifacts apply only when that export is active.
Incompatible duplicate instances in one final linkage closure are errors unless
the package and platform explicitly support coexistence.

## Consequences

Host tools and target libraries can use separate realizations of one package
release. Multi-export packages remain precise. Runtime files and notices attach
to the correct activation unit. Diagnostics and explanation show the path that
created each PackageInstance and activated each export.
