---
title: Troubleshoot build and stage
description: Diagnose restore, generator, compiler, linker, missing artifact, and stage-plan failures.
---

# Troubleshoot build and stage

## Identify the failing phase

Run phases separately with the same selection:

```bash
ngin restore --project App.nginproj --profile Debug
ngin configure --project App.nginproj --profile Debug
ngin build --project App.nginproj --profile Debug
ngin stage --project App.nginproj --profile Debug
```

The first failing command owns the diagnosis. A later `run` failure can be a
staging problem, but a compiler error is not.

## Package restore or lock fails

```bash
ngin package verify-lock --lock ngin.lock
ngin graph --format json --project App.nginproj --profile Debug
```

Check the requested version/interval, provider, package source, activation
conditions, host versus target context, and selected exports. A lock records
exact acquired instances; the graph fingerprint separately records semantic
activation and selection. Do not hand-edit hashes to make verification pass.

## Generator or tool action fails

Generators run only when selected through `<Generate>`/the action model.
Analyzers and formatters are separate explicit operations. Confirm the package,
action export, backing tool, host platform, inputs, and output path in the
graph.

Generated output belongs in the selected build tree. Fix the authored header,
manifest, package action, or generator implementation—not the generated file.

## Compiler error

Read the first compiler diagnostic, then inspect the generated compile command
or build backend output for include paths, definitions, language mode, and
source ownership. Trace an unexpected include/definition to its graph
provenance with `ngin explain`.

Common causes:

- source glob does not include the intended file;
- a package exports headers but the product did not select that export;
- target and host tool dependencies were confused;
- the required C++ standard/compiler SDK is absent;
- a public header relies on an accidental transitive include.

## Linker error

- Undefined symbol: the declaration was compiled but its owning library/export
  is not linked, or ABI/configuration differs.
- Duplicate symbol: the same implementation entered more than one target, or a
  non-inline definition lives in a header.
- Wrong architecture/format: host and target artifacts were mixed.

Use the graph to verify link exports and artifact identity before adding raw
linker flags.

## Stage says an artifact is missing

Stage consumes a typed StagePlan. Confirm that build succeeded for the exact
configuration/target and that the product/package declared the runtime file,
shared library, plugin, config, asset, or notice as stage intent.

Do not add a post-build copy to generated CMake. Add the authored Stage/package
metadata so `run`, `test`, publish, editor tooling, and clean rebuilds share the
same truth.

## Atomic or permission failures

Check destination permissions, locks held by a running executable, path length,
cross-device moves, antivirus/indexer interference, and available space. Keep
the native/system error code from the diagnostic; portable wording alone may
not identify the OS cause.

