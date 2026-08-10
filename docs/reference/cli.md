# CLI reference

Run `ngin help` for the command list supported by the executable. The CLI has
one authoring and resolution path: every lifecycle command resolves the direct
XML model into a Composition Graph and then derives a typed plan.

## Selection

Project commands accept:

```text
--project <file.nginproj>
--workspace <file.ngin>
--configuration <name>
--target <name-or-alias>
--toolchain <name>
--option <Name=Value>
--preset <name>
```

Presets expand command inputs before resolution. Their names do not enter the
Composition Graph identity. An unknown selection is an error; it never falls
back to the first workspace entry.

## Authoring

```text
ngin new <app|lib|tool|test|benchmark|plugin|external> <Name>
ngin add package <Name> [--exact V|--compatible V|interval flags]
                        [--use Kind:Name] [--option Name=Value]
ngin add project-reference <Path>
ngin add action <Package::Action> --kind <Generate|Analyze|Format|Validate|Custom>
ngin package add|update|remove <Name> [dependency options]
ngin manifest format --project <file.nginproj>
ngin schema --format json
```

Readable interval flags are `--at-least`, `--after`, `--at-most`, and
`--before`. They produce a structured `<Version>` element and avoid encoded XML
comparison operators.

## Validation and explanation

```text
ngin validate
ngin inspect --format json
ngin graph --format json
ngin explain <graph-identity> [--format json]
ngin diff --against <other.nginproj> [--format json]
```

`validate` performs structural and document-local semantic validation.
`inspect` and `graph` also resolve packages, exports, options, capabilities,
actions, provenance, and the selected target facts. `diff` returns `0` when the
graphs are equivalent and `2` when semantic differences exist.

## Build and deployment

```text
ngin configure [--output <dir>]
ngin build [--output <dir>]
ngin stage [--output <dir>]
ngin run [--output <dir>] [-- process arguments]
ngin test [--output <dir>] [-- process arguments]
ngin publish [PublishName] [--output <dir>]
```

CMake is the only implemented build adapter. Configure and build consume the
derived CMake BuildPlan; stage, launch, test, and publish consume their own
backend-neutral plans. `run` and `test` use native process execution with the
planned working directory, environment, and timeout.

## Actions

```text
ngin analyze [--lock <ngin.lock>] [--output <dir>]
ngin format [--lock <ngin.lock>] [--output <dir>]
```

Only explicitly selected Actions of the requested kind execute. The CLI
resolves their Tool exports in host package context, verifies workspace trust
policy, configures the CMake tool binding, and invokes the derived ActionPlan.
Normal builds execute only selected `Generate` Actions; analyzers and formatters
do not run as a side effect of package presence or `ngin build`.

## Packages and reproducibility

```text
ngin restore
ngin package lock [--output <ngin.lock>]
ngin package verify-lock [--lock <ngin.lock>]
```

The dependency lock records exact acquired PackageInstances, including
provider identity, host/target context, binary compatibility, integrity,
artifact options, trust, and signature provenance. The separate Composition
Graph fingerprint covers semantic activation and does not turn the dependency
lock into a second graph lock.

## Exit behavior

Invalid syntax, failed validation or resolution, stale locks, denied Actions,
backend failures, staging failures, and failed child processes return nonzero.
Diagnostics are written to stderr; graph JSON is written to stdout.
