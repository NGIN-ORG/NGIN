# Manifest authoring model

NGIN has one authored model:

```text
Friendly .ngin* XML
        -> normalized Manifest IR
        -> resolved Composition Graph
        -> typed Build, Action, Stage, Run, Test, Benchmark, and Publish plans
```

Authors describe intent; backend details and resolver mechanics stay below the
Manifest IR boundary.

## Document roles

- `.nginproj` describes one source `Executable` or `Library` product, its Uses,
  build inputs, actions, additive conditions, and deployment or execution
  registrations.
- `.nginpkg` describes typed package exports, NGIN-specific CPS overlays,
  capabilities, actions, assets, notices, staging contributions, and backend
  adapter bindings.
- `.ngin` discovers projects and packages and owns shared versions, profiles,
  capability preferences, and trust policy.

Executable and Library are the only physical product kinds. Test and Benchmark
are Executable registrations. Tool is a package role for an executable. Plugin
is a loadable Library kind and package role. CxxModule names a C++ source item.
NGIN.Core runtime modules remain in application code or application-owned
configuration.

## Authoring laws

- Uses is typed; no generic dependency union exists.
- Version requests are compatible by default, with explicit Exact or bounded
  intervals when required.
- Build and Stage collections are repeated typed elements.
- When is shallow, typed, additive, and order-independent.
- Selecting an action introduces its host package and Tool exactly once.
- One Executable receives an implicit default Run.
- Capability resolution yields exactly one compatible implementation.
- CPS owns portable compiled components and usage requirements.
- Adapters remain outside Composition Graph identity.
- Built-in defaults and inferred facts are always inspectable with provenance.

The exact contracts are split by document type:

- [Project manifests](project-manifest.md)
- [Package manifests](package-manifest.md)
- [Workspace manifests](workspace-manifest.md)
- [Composition Graph](composition-graph.md)
- [CLI](cli.md)
