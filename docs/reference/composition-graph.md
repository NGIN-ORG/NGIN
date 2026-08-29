# Composition Graph

The Composition Graph is NGIN's immutable, fully resolved semantic contract.
Friendly XML first lowers into normalized Manifest IR; package resolution then
produces the graph. Build, action, stage, run, test, benchmark, publish, editor,
diff, and explanation behavior derives from that graph or a typed plan.

The graph is backend-free. CMake target names, cache variables, generated
commands, acquired filesystem roots, and adapter fields do not enter it.

An explicitly registered CMake Workspace Project is inspected through CMake's
File API and CTest. That tagged editor model is deliberately not synthesized
into an NGIN Composition Graph: CMake remains authoritative for its targets,
sources, compile groups, artifacts, and tests.

## Resolution boundary

Before a graph exists, NGIN resolves:

- the Executable or Library product and effective selection;
- project and package Options;
- project, package, capability, host, and target requirements;
- exact package instances and activated typed exports;
- capability implementations and workspace preferences;
- selected actions and their backing host Tools;
- Plugins and stage contributions; and
- Run, Test, Benchmark, and Publish intent.

Ambiguous, incompatible, or unresolved decisions are errors. Graph consumers do
not need the authored XML tree or a PackageProvider.

## Canonical shape

```json
{
  "kind": "NGIN.CompositionGraph",
  "state": "resolved",
  "product": {
    "artifactKind": "Executable",
    "libraryKind": "None"
  },
  "selection": {},
  "options": [],
  "packages": [],
  "exports": [],
  "capabilityBindings": [],
  "actions": [],
  "plugins": [],
  "contributions": [],
  "buildItems": [],
  "runs": [],
  "tests": [],
  "benchmarks": [],
  "publishes": [],
  "edges": []
}
```

`artifactKind` is `Executable` or `Library`. `libraryKind` is `Static`,
`Shared`, `Interface`, or `Plugin` for a Library and `None` for an Executable.
Tool and Plugin package exports remain separate nodes. Test and Benchmark are
registration collections, not product kinds.

## Identity and determinism

Identity-bearing collections are sorted by semantic identity. Authored order
does not select dependencies, capability providers, or conflict winners.
Profile labels, checkout roots, backend bindings, and secret values are absent.
Equivalent resolved inputs therefore serialize byte-identically and produce the
same `sha256:` Composition Identity.

Dependency locks and derived plan fingerprints are intentionally separate.
See [dependency-lock.md](dependency-lock.md) and
[deployment-plans.md](deployment-plans.md). Editors consume the graph through
the versioned [editor protocol](editor-protocol.md), which adds authored-file
preconditions and distinguishable path roles without becoming a second resolver.

## Node inventory

- Product records name, version, artifact kind, library kind, and language
  settings.
- Selection records resolved configuration, target OS and architecture,
  compiler, runtime, optimization, debug symbols, and LTO.
- Package instances record exact provider results and host/target context.
- Exports are activated Library, Tool, Plugin, Action, or Asset roles.
- Capability bindings connect one requirement to exactly one compatible
  provider export.
- Actions identify their host Tool, deterministic contract, inputs, and outputs.
- Plugins describe available loadable artifacts only; they never imply runtime
  registration or lifecycle.
- Contributions and build items preserve typed paths, visibility, ownership,
  and provenance.
- Runs, Tests, Benchmarks, and Publishes retain backend-neutral execution or
  distribution intent.
- Edges preserve the causality of requirements, activations, capability
  bindings, and host Tool use.

## Provenance and effective authoring

Resolved facts carry logical document, source location, owner, source kind, and
reason. Built-in and inferred facts first appear in Manifest IR with explicit
provenance and remain explainable after resolution.

- `ngin inspect --effective` displays normalized Manifest IR.
- `ngin graph --format json` emits this graph.
- `ngin diff` compares semantic identities.
- `ngin explain` traces a graph identity through provenance and edges.

Runtime services, dependency injection, module startup ordering, and plugin
loading are application or NGIN.Core concerns and are excluded from both
Manifest IR and the Composition Graph.
