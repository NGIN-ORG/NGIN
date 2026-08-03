# Composition Graph JSON

The Composition Graph is NGIN's resolved project model. Editors, build
generation, staging, runtime integration, tests, and publishing consume this
result rather than interpreting manifests independently.

Emit the full graph with:

```bash
ngin inspect --format json
ngin graph --format json
```

The full envelope uses:

```json
{
  "schemaVersion": "4.0",
  "kind": "NGIN.CompositionGraph",
  "state": "resolved"
}
```

The normative JSON Schema is
[`../schemas/ngin-composition-graph-v4.schema.json`](../schemas/ngin-composition-graph-v4.schema.json).

## Main fields

The graph identifies the selected workspace, output root, output directory,
project, product, profile, platform, toolchain, environment, conventions, and
resolved properties. Its `plans` object contains package, package-feature,
build, editor, generator, stage, runtime, environment, launch, package-output,
publish, tooling, and diagnostic data.

Focused graph commands return `NGIN.CompositionGraphPlan` with the same schema
version and one plan in `data`. Supported CLI switches include:

```text
--build-plan       --editor-plan       --stage-plan
--package-plan     --package-output-plan
--launch-plan      --runtime-plan      --environment-plan
--publish-plan     --tooling-plan
```

## Compatibility

Within schema `4.x`, producers may add optional fields and consumers must ignore
unknown fields. Removing or renaming documented fields, changing their types,
or changing secret-redaction semantics requires a new major graph schema.

## Provenance and secrets

Selected contributions carry their source kind, source name, manifest path,
and reason where the resolver can identify them. This explains the winning
value; it is not a log of every discarded candidate.

Secret environment values are always redacted. Consumers must not attempt to
recover them from provenance, diagnostics, or related fields.
