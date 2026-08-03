# CLI reference

Run `ngin` without arguments for the command list supported by the executable
you are using. That output is authoritative; this page groups the commands by
task.

## Project lifecycle

```text
ngin validate
ngin configure
ngin build
ngin stage
ngin clean
ngin rebuild
ngin run
ngin test
ngin benchmark
ngin publish [PublishName]
```

Project commands accept `--project`, `--workspace`, and `--profile`. Commands
that write build output accept `--output <dir>` or
`--output-root <dir>`, where supported.

## Create and edit

```text
ngin new <app|lib|tool|test|benchmark|plugin> <Name>
ngin add package <Name> --version <range>
ngin add project-reference <Path>
ngin add tool-action <Package::Action>
ngin manifest format
ngin settings init
```

## Inspect and explain

```text
ngin graph
ngin inspect --format json
ngin diff --from-profile <name> --to-profile <name>
ngin diff --from-lock <file> --to-lock <file>
ngin explain <kind>:<identity>
ngin explain condition <Name>
ngin explain package-feature <Package> <Feature>
ngin explain generator <Name>
ngin variables explain
ngin schema --format json
```

`inspect` is the machine-readable, non-mutating project view. `graph` also
offers focused build, editor, stage, package, launch, runtime, environment,
publish, and tooling plans.

## Packages

```text
ngin restore [--locked]
ngin package list
ngin package show <Name>
ngin package add|remove|update ...
ngin package sources list|add|remove ...
ngin package pack
ngin package lock
ngin package verify-lock
```

## Workspaces

```text
ngin workspace list
ngin workspace status
ngin workspace doctor
```

The top-level aliases `ngin list`, `ngin status`, and `ngin doctor` are also
accepted.

## Tools and quality

```text
ngin analyze
ngin format
ngin scan
ngin report
ngin quality
ngin quality baseline create|update|verify
ngin tool list
ngin tool plan
ngin tool doctor
ngin tool run <RunName>
ngin tool results
ngin tool edits
```

## Output

Common output controls include `--quiet`, `--verbose`, `--trace`, `--plain`,
`--color`, `--ui`, `--backend-output`, and `--events jsonl`.

Lifecycle JSONL output uses the `NGIN.CLI.Event` `1.0` envelope. In event mode,
stdout contains event objects only. Consumers must ignore unknown fields and
event types within the same major schema.

## Exit behavior

Invalid commands, invalid manifests, failed resolution, backend failures, and
failed process execution return a nonzero status. Tool commands use more
specific statuses documented in the [tool driver reference](tool-driver.md).
