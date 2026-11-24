# CLI reference

Run `ngin` without arguments for the command list supported by the current
executable. Every command uses one authored grammar, lowers it to Manifest IR,
and resolves semantic work through the Composition Graph.

Use `ngin run --help` or `ngin help run` for usage and an example.
Nested commands support the same syntax, for example `ngin package add --help`.
In Bash, enable completion for commands, subcommands, and their options with
`source <(ngin completion bash)`. Arguments after `--` go to the application,
including `--help`.

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

Explicitly registered CMake workspace projects use a project directory and
CMake-specific operation selection:

```text
--project <cmake-project-directory>
--configure-preset <name>
--configuration <name>          # when relevant to a multi-config generator
--build-preset <name>            # build operation only
--test-preset <name>             # test operation only
--target <file-api-target-id>     # build one target
--test-name <ctest-name>          # repeat to run selected tests
```

Configure presets are persistent CMake context. Build and test presets are
operation choices. NGIN delegates configure, build, and test execution to
`cmake` and `ctest`; it does not reinterpret arbitrary CMake source or create
presets. CMake projects do not support NGIN Stage, Run, Debug, Benchmark, or
Composition Graph commands in version 1.

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
ngin editor workspace
ngin editor snapshot
ngin editor plan --intent <CreateItems|IncludeItems|ExcludeItems|RenameItems|MoveItems|DeleteItems|AddPackage|ChangePackageRequirement|RemovePackage>
                 --item <Kind|Visibility|relative-path>
```

`--version` is a compatibility request. Readable interval flags are
`--at-least`, `--after`, `--at-most`, and `--before`; they author a structured
`Version` child. `ngin format` performs stable canonical manifest formatting,
while `ngin format --check` reports files that would change without writing.

`editor workspace`, `editor snapshot`, and `editor plan` expose the versioned, non-mutating editor
contract. Plans contain precondition hashes and minimal manifest text edits;
the caller applies them through its own workspace transaction. See the
[editor protocol](editor-protocol.md).

`editor workspace` protocol version 2 returns mixed `Ngin` and `CMake`
Workspace Projects with stable IDs and explicit capabilities. For a CMake
project, `editor snapshot --project <directory>` safely lists preset display
metadata without configuring. Supplying `--configure-preset` reads the newest
CMake File API reply and returns configurations, directories, targets, sources,
compile groups, artifacts, declaration backtraces, toolchains, stale state,
and CTest tests. Snapshots whitelist fields and omit cache/environment values.

## Validation, effective inspection, and explanation

```text
ngin validate
ngin inspect --effective
ngin inspect --format json
ngin graph --format json
ngin explain <project-relative-file|graph-identity> [--format json]
ngin diff --against <other.nginproj> [--format json]
```

`validate` performs structural and document-local semantic validation.
`inspect --effective` emits normalized Manifest IR before package resolution,
including matching `When` additions, built-in defaults, implicit Runs, and
registration provenance. `graph` emits the canonical resolved Composition
Graph. `diff` returns `0` for equivalent graphs and `2` for semantic changes.

From `Examples/Hello.Native`, run `ngin explain src/main.cpp` to see which
rule includes the file and where it was written. For a file outside the build,
the explanation reports matching exclusions or inactive rules for the selected
configuration. Add `--format json` for structured provenance.

## Build and deployment

```text
ngin configure [--output <dir>]
ngin build [--output <dir>]
ngin stage [--output <dir>]
ngin run [--output <dir>] [-- process arguments]
ngin test [registration] [--output <dir>] [-- process arguments]
ngin benchmark [--output <dir>] [-- process arguments]
ngin publish [PublishName] [--output <dir>]
```

The current build adapter generates CMake. Stage, Run, Test, Benchmark, and
Publish each consume their own backend-neutral typed plan. An Executable has an
implicit default Run. Test and Benchmark execute their respective registrations
and do not change the product artifact kind.

To prepare an invocation for an external launcher, use `ngin run --launch-plan`
or `ngin test NAME --launch-plan`. These build and stage the selected product,
then print a JSON invocation without starting the application. Add `--no-stage`
to reuse an existing build and stage. See the [launch contract](editor-protocol.md#launch).

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
