# Separate dependency locks from composition fingerprints

## Status

Accepted

## Context

A package lock must reproduce acquisition, while a project may have many valid
Configurations, Targets, Toolchains, Options, active exports, Actions, and
capability bindings. Storing the entire semantic composition in one global
package lock makes unrelated builds invalidate each other and duplicates the
Composition Graph.

## Decision

The dependency lock records exact acquired PackageInstances: coordinate,
PackageProvider identity/revision, source or binary integrity, host/target
context, derived compatibility facts used by the artifact,
artifact-affecting options, provider-native artifact identity, and hermeticity.

Selection enters the dependency lock only when it changes acquisition,
dependency closure, or produced/selected artifacts. Activating an export or
Action is not itself copied into the lock, but every PackageInstance caused by
that activation is locked.

The canonical Composition Graph has a separate derived composition fingerprint
covering concrete selection, active exports, capability bindings, Actions,
Tools, Plugins, edges, contributions, and exact PackageInstance references.
Execution plans may have additional plan fingerprints for backend/executor
caching.

## Consequences

Locked resolution guarantees dependency acquisition identity. A matching
composition fingerprint guarantees semantic composition identity. The
dependency lock does not become a second graph, and composition-only changes do
not invalidate acquired artifacts unnecessarily.
