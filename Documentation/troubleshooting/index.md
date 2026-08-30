---
title: Troubleshooting NGIN
description: Diagnose install, manifest, restore, build, stage, run, plugin, and runtime failures without editing generated output.
---

# Troubleshooting NGIN

Start with the first command that fails. Keep its complete stderr output and
the exact project, profile, configuration, and toolchain selection.

## Fast diagnostic sequence

Run these from the workspace root, replacing the project path:

```bash
ngin validate --project path/to/App.nginproj --profile Debug
ngin inspect --effective --project path/to/App.nginproj --profile Debug
ngin graph --format json --project path/to/App.nginproj --profile Debug
```

This separates three failure classes:

1. `validate` fails: the authored document or local semantics are invalid.
2. `validate` passes but `graph` fails: selection, package resolution, or
   composition failed.
3. `graph` passes but build/stage/run fails: a backend, toolchain, artifact, or
   runtime operation failed.

Do not edit `.ngin/build/`, `build/`, staged layouts, generated CMake, or
`*.nginlaunch`. They are evidence and disposable output, not authored inputs.

## Choose the failure area

| Failure | Page |
| --- | --- |
| CLI is missing, configure cannot find tools, or a manifest is rejected | [Install and configure](./install-configure.md) |
| Compile, link, package restore, generation, or stage fails | [Build and stage](./build-stage.md) |
| Executable, dynamic module, plugin, or runtime service fails | [Run and plugins](./run-plugins.md) |
| You need a useful report for an issue or another developer | [Get diagnostic data](./diagnostics.md) |

## Read diagnostics literally

NGIN diagnostics go to stderr. Machine-readable output requested with
`--format json` goes to stdout. A nonzero exit means the requested operation
did not complete; do not continue a script by assuming partial output is valid.

When a diagnostic includes a graph identity or provenance, use:

```bash
ngin explain <graph-identity> --format json \
  --project path/to/App.nginproj --profile Debug
```

That is faster and more reliable than reverse-engineering generated CMake.

