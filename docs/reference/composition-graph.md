# Composition Graph

The Composition Graph is NGIN's immutable, fully resolved semantic model. It is
the boundary between authoring and execution: manifests and PackageProviders
are interpreted once, and every build, generation, staging, launch, test,
publish, editor, inspection, and explanation operation consumes the resulting
graph or a plan derived from it.

The graph is deliberately backend-free. It describes what the selected product
is composed from, not how CMake or another integration happens to express it.
CMake targets, cache variables, generated commands, and integration bindings do
not belong in the graph.

## Resolution boundary

The resolver completes these decisions before constructing the graph:

- the product and effective target selection;
- project and package Option values;
- the fixed-point dependency closure and exact PackageInstances;
- host versus target package context;
- active Exports and their transitive requirements;
- Capability implementation bindings;
- selected Actions and their host Tools;
- selected Plugins, contributions, and generated build items;
- backend-neutral Launch, Testing, and Publish intent; and
- dependency, activation, capability, and Tool-use edges.

A successful graph therefore needs neither the authored XML tree nor a
PackageProvider to be understood. If any decision is ambiguous, incompatible,
or unresolved, resolution fails and no graph is produced.

## Identity and immutability

`ResolvedCompositionGraph` owns an immutable snapshot. Its collections are
sorted by semantic identity before serialization. Callers receive only a const
view of the graph data.

Package identity includes the exact PackageProvider result, host or target
context, derived binary compatibility, and artifact-affecting Options. Host and
target instances are separate even when their package name and version match.
Export, Action, Plugin, contribution, build-item, Launch, Testing, Publish, and
edge identities are based on their semantic owners rather than XML position.

Equivalent target selections produce the same canonical graph. Incidental
preset labels and checkout roots are not semantic facts and do not affect the
result. PackageProvider identities stored in the graph must likewise be stable,
logical identities rather than absolute acquisition paths.

## Canonical representation

Canonical JSON has this top-level shape:

```json
{
  "kind": "NGIN.CompositionGraph",
  "state": "resolved",
  "product": {},
  "selection": {},
  "options": [],
  "packages": [],
  "exports": [],
  "capabilityBindings": [],
  "actions": [],
  "plugins": [],
  "contributions": [],
  "buildItems": [],
  "launches": [],
  "testing": null,
  "publishes": [],
  "edges": []
}
```

The pre-release authoring model does not publish a numbered graph schema. A
number will be assigned only when the model is officially released. Until then,
the repository's model types, focused tests, and this page define the format.

Serialization is deterministic:

- object keys use canonical ordering;
- identity-bearing collections are sorted by identity;
- authored collection order does not select a winner;
- paths are portable manifest paths or workspace-relative provenance documents;
- absolute checkout roots and PackageProvider acquisition roots are excluded;
- backend bindings and commands are excluded; and
- repeated equivalent resolution is byte-stable.

The Composition Identity is the lowercase `sha256:` fingerprint of a typed
canonical envelope containing the complete resolved graph. The dependency lock
is derived separately and contains only acquired PackageInstances. Build and
Action plans likewise have their own SHA-256 fingerprints, so backend inputs do
not silently change semantic graph identity. See
[dependency-lock.md](dependency-lock.md).

## Node inventories

### Product and selection

The product records its stable identity, name, product type, optional package
version, and Library linkage where applicable. Selection records configuration,
target operating system and architecture, compiler identity and version, and
runtime library. Preset names are intentionally absent.

### Options

Each resolved Option records its owner, typed canonical value, whether it
affects artifact identity, and provenance. Project and package Options share the
same graph concept while retaining distinct owners.

### PackageInstances and Exports

A PackageInstance records its exact coordinate, PackageProvider kind and logical
identity, revision and integrity facts, host or target context, hermeticity,
derived binary compatibility, and artifact-affecting Options. An Export node is
an activated semantic Library, Tool, Plugin, Action, or Asset owned by an exact
PackageInstance.

### CapabilityBindings

A CapabilityBinding connects a requirement to one compatible implementation.
It records the capability name and domain, selected implementation version,
owning PackageInstance, and Export. Zero implementations and multiple compatible
implementations are errors; the resolver does not guess.

### Actions, Plugins, and contributions

Actions name their host PackageInstance, Action Export, Tool Export,
determinism declaration, and declared outputs. Plugins are explicit activated
Plugin Exports. Contributions are semantic notices, runtime files or
directories, and asset files or directories; staging destinations remain
portable and are not backend commands.

### Build items

Build items describe resolved Sources, Headers, C++ Modules, Resources, include
directories, defines, compile and link options, and precompiled headers.
Conventional and authored inputs use the same node form. Action outputs become
generated build items only after their Action and host Tool are resolved. A
define records its name in `path` and preserves its optional replacement text
in `value`, so graph consumers do not need to reconstruct authored semantics.

### Deployment and process intent

Project and package Stage inputs use contribution nodes with portable sources,
destinations, ownership, and provenance. Launch nodes retain the selected
Product or Tool Export, repeated arguments, working directory, ordinary
environment values, and external secret references. Secret contents never enter
the graph. Testing records arguments and timeout policy. Publish nodes record
only Folder, Archive, or Installer intent, format, and portable output; CPack
settings are absent.

### Edges

Edges make resolution causality explicit. Current semantic edge kinds include
project dependency, package requirement, Export activation, Capability binding,
and host Tool use. Visibility and host/target or dependency context are stored
where applicable.

## Provenance

Every non-trivial resolved fact carries source and resolution provenance:

- semantic source kind and owner;
- a logical document path;
- source line and column; and
- the reason the fact entered the graph.

Capability nodes point to the requirement that demanded the binding, while
their binding edges point to the selected implementation. This preserves both
sides of the decision. Provenance paths are workspace-relative when possible;
sources outside the workspace use only a logical document name, never an
absolute checkout path.

## Inspect, diff, and explain

Graph inspection operates on semantic identities:

- `inspect` enumerates the immutable graph or a focused identity inventory;
- `diff` reports added, removed, and semantically changed identities by
  category; and
- `explain` returns the selected value, its provenance, and incoming or outgoing
  edges for one identity.

Diff covers the product, selection, Options, PackageInstances, Exports,
CapabilityBindings, Actions, Plugins, contributions, build items, Launches,
Testing, Publishes, and edges.
Formatting and XML ordering are not reported as semantic changes.

The CLI command migration to these APIs is tracked separately from the graph
model itself. Commands must not regain access to authored AST nodes or the
previous resolved/profile aggregate once they have crossed this boundary.

## Derived plans and integrations

BuildPlan, ActionPlan, StagePlan, and other executable plans are derived from
this graph. A build-system adapter may combine the graph with an immutable
integration sidecar keyed by graph identities. For example, the CMake adapter
may map a semantic Export to an imported CMake target, but that target name
exists only in the CMake sidecar and derived BuildPlan.

This separation keeps the authoring model usable for beginners while leaving a
precise extension boundary for future build systems and package providers. Only
the CMake adapter is implemented in the current scope; the graph does not claim
that other adapters already exist.

Stage, process, test, and publishing behavior is specified in
[deployment-plans.md](deployment-plans.md).
