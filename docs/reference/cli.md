# CLI reference

Run `ngin` without arguments for the command list supported by the current
executable. Every command uses one authored grammar, lowers it to Manifest IR,
and resolves semantic work through the Composition Graph.

## Selection

Project commands accept:

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
before resolution. A profile label does not enter graph identity. Explicit
selection that conflicts with a profile is an error.

## Authoring and formatting

```text
ngin new executable <Name>
ngin new library <Name>
ngin add package <Name> [--exact V|--version V|version interval flags]
                        [--export Kind:Name] [--option Name=Value]
ngin add project-reference <Path>
ngin add action <Package::Action> --kind <Generate|Analyze|Format|Validate|Custom>
ngin package add|update|remove <Name> [version and activation options]
ngin format [--check] [--project <manifest>]
ngin schema --format json
```

`--version` is a compatibility request. Readable interval flags are
`--at-least`, `--after`, `--at-most`, and `--before`; they author a structured
`Version` child. `ngin format` performs stable canonical manifest formatting,
while `ngin format --check` reports files that would change without writing.

## Validation, effective inspection, and explanation

```text
ngin validate
ngin inspect --effective
ngin inspect --format json
ngin graph --format json
ngin explain <graph-identity> [--format json]
ngin diff --against <other.nginproj> [--format json]
```

`validate` performs structural and document-local semantic validation.
`inspect --effective` emits normalized Manifest IR before package resolution,
including matching `When` additions, built-in defaults, implicit Runs, and
registration provenance. `graph` emits the canonical resolved Composition
Graph. `diff` returns `0` for equivalent graphs and `2` for semantic changes.

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

The current build adapter generates CMake. Stage, Run, Test, Benchmark, and
Publish each consume their own backend-neutral typed plan. An Executable has an
implicit default Run. Test and Benchmark execute their respective registrations
and do not change the product artifact kind.

## Actions

```text
ngin analyze [--file <source>]... [--format json] [--lock <ngin.lock>] [--output <dir>]
ngin tooling format [--file <source>]... [--lock <ngin.lock>] [--output <dir>]
```

Only explicitly selected actions of the requested kind execute. Selection of a
Generator, Analyzer, Formatter, or Validator introduces its package, action
export, and backing Tool in host context. Package presence alone never runs an
action. Normal builds execute selected Generate actions only.

## Packages and reproducibility

```text
ngin restore
ngin package lock [--output <ngin.lock>]
ngin package verify-lock [--lock <ngin.lock>]
```

The dependency lock records exact acquired package instances. The Composition
Graph fingerprint separately covers semantic activation and selection.

## Exit behavior

Invalid syntax, failed resolution, stale locks, denied actions, backend or
staging failures, and failed child processes return nonzero. Diagnostics go to
stderr; requested machine-readable payloads go to stdout.
