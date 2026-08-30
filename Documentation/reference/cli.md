---
title: CLI command map
description: Find NGIN commands by authoring, inspection, build, action, package, and editor responsibility.
---

# CLI command map

Run `ngin` without arguments for the exact command list supported by the
installed executable.

Every NGIN project command reads the same authored grammar, lowers it to
Manifest IR, and resolves semantic work through the Composition Graph. The
command chooses a consumer of that graph; it does not introduce a second
interpretation of the manifest.

## Common selection

```text
--project <file.nginproj>
--workspace <file.ngin>
--configuration <name>
--target <name-or-alias>
--toolchain <name>
--run <name>
--profile <name>
--option <Name=Value>
```

Profiles expand to configuration, target, toolchain, Run, and Option facts
before resolution. Explicit selection that conflicts with a profile is an
error.

## Authoring and formatting

```text
ngin new executable <Name>
ngin new library <Name>
ngin add package <Name> [version options]
ngin add project-reference <Path>
ngin add action <Package::Action> --kind <Generate|Analyze|Format|Validate|Custom>
ngin package add|update|remove <Name> [version and activation options]
ngin format [--check]
ngin schema --format json
```

`ngin format` writes canonical manifest formatting. `--check` reports drift
without modifying the document. `--version` is a compatibility request;
`--exact`, `--at-least`, `--after`, `--at-most`, and `--before` express exact
or interval requirements.

## Validation and explanation

```text
ngin validate
ngin inspect --effective
ngin inspect --format json
ngin graph --format json
ngin explain <graph-identity> [--format json]
ngin diff --against <other.nginproj> [--format json]
```

`validate` checks structural and local semantics. `inspect --effective` shows
normalized manifest IR. `graph` shows resolved composition.

`diff` exits `0` when graphs are equivalent and `2` when they differ. Treat
other nonzero exits as failure, not a semantic difference.

## Build and deployment

```text
ngin configure [--output <dir>]
ngin build [--output <dir>]
ngin stage [--output <dir>]
ngin run [--output <dir>] [-- process arguments]
ngin test [--output <dir>] [-- process arguments]
ngin benchmark [--output <dir>] [-- process arguments]
ngin publish [PublishName] [--output <dir>]
```

## Actions

```text
ngin analyze [--file <source>]... [--format json]
ngin tooling format [--file <source>]...
```

Only explicitly selected actions of the requested kind execute. Normal builds
execute selected Generate actions only.

`analyze` and `tooling format` may accept repeated `--file`, a lock path, and
an output directory. Package presence alone never executes an action.

## Packages and reproducibility

```text
ngin restore
ngin package lock [--output <ngin.lock>]
ngin package verify-lock [--lock <ngin.lock>]
```

## Editor protocol

```text
ngin editor workspace
ngin editor snapshot
ngin editor plan --intent <intent> --item <item>
```

Editor plans are non-mutating. They contain precondition hashes and minimal
text edits for the caller to apply through its own workspace transaction.

Protocol consumers must check the returned version. Workspace protocol v2 can
describe NGIN and explicitly registered CMake projects with stable IDs and
capability flags. Snapshot output whitelists fields and omits cache/environment
values.

## Explicit CMake projects

CMake workspace projects use a project directory rather than `.nginproj` and
accept CMake operation selection:

```text
--project <cmake-project-directory>
--configure-preset <name>
--configuration <name>
--build-preset <name>
--test-preset <name>
--target <file-api-target-id>
--test-name <ctest-name>
```

Configure presets are persistent context; build and test presets select one
operation. NGIN delegates to CMake/CTest and does not reinterpret arbitrary
CMake source or create presets. In the current contract, these projects do not
support NGIN Stage, Run, Debug, Benchmark, or Composition Graph operations.

## Output and exit behavior

Diagnostics are written to stderr. Requested JSON or other machine-readable
payloads are written to stdout. Invalid syntax, failed resolution, stale locks,
denied actions, backend/stage failures, and failed child processes return a
nonzero exit.

Scripts should check the exit code before consuming stdout and should not parse
human diagnostics when a versioned JSON form exists.

## Failure lookup

| Failure | First command/page |
| --- | --- |
| Manifest rejected | `ngin validate`; [install and configure](../troubleshooting/install-configure.md) |
| Unexpected effective value | `ngin inspect --effective` |
| Unexpected package/link/stage input | `ngin graph`, then `ngin explain` |
| Lock mismatch | `ngin package verify-lock` |
| Backend or staging error | [build and stage](../troubleshooting/build-stage.md) |
