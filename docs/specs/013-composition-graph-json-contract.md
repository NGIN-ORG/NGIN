# NGIN Composition Graph JSON Contract

Status: V4 Contract Refreeze In Progress

The unshipped phase-one `quality.analyzers` slice has been removed under the
approved tooling-framework breaking-change policy. The replacement is the
general `tooling.runs` plan. No consumer compatibility alias is provided.

This document defines the stable machine-readable Composition Graph JSON
contract emitted by:

```bash
ngin inspect --format json
ngin graph --format json
ngin graph --format json --<plan>-plan
```

The normative schema artifact is:

```text
docs/schemas/ngin-composition-graph-v4.schema.json
```

## Versioning

The V4 graph contract uses:

```json
{
  "schemaVersion": "4.0",
  "kind": "NGIN.CompositionGraph"
}
```

Focused plan output uses the same schema version with:

```json
{
  "schemaVersion": "4.0",
  "kind": "NGIN.CompositionGraphPlan"
}
```

Within `schemaVersion` `4.x`, producers may add optional fields and consumers
must ignore unknown fields. Removing a documented field, changing a documented
field type, renaming an enum value, changing secret redaction semantics, or
moving plan data to a different path requires a new major graph schema version.

The manifest schema version and graph schema version intentionally share the
V4 number because V4 is the first product-first graph-native authoring model.
They are still separate contracts: XML authoring changes do not automatically
change graph JSON, and graph JSON additions do not automatically change XML.

## Full Graph Envelope

The full graph envelope contains these stable top-level fields:

- `schemaVersion`: literal `"4.0"`
- `kind`: literal `"NGIN.CompositionGraph"`
- `state`: `"resolved"` or `"diagnostic"`
- `facets`: supported facet names
- `workspace`: selected workspace identity and manifest path, or `null`
- `outputRoot`: resolved workspace artifact root
- `outputDir`: resolved staged output directory for the selected project/profile
- `identity`: selected project/product/profile identity
- `conventions`: named defaults that participated in resolution
- `properties`: high-value selected scalar properties with provenance
- `product`: selected product kind and output identity
- `selection`: selected profile/platform/toolchain/environment/ABI context
- `facetsSummary`: numeric counts by facet
- `plans`: graph-owned plan slices

The stable `plans` object contains:

- `packages`
- `packageFeatures`
- `build`
- `generators`
- `stage`
- `runtime`
- `environment`
- `launch`
- `launches`
- `packageOutputs`
- `publish`
- `tooling`
- `diagnostics`

Consumers should prefer the plan slices over legacy command-local text output.
Editor integrations should use `ngin inspect --format json`; command-line graph
review tools may use either `inspect` or `graph`.

## Focused Plan Envelope

Focused graph plans use:

```json
{
  "schemaVersion": "4.0",
  "kind": "NGIN.CompositionGraphPlan",
  "plan": "build",
  "state": "resolved",
  "identity": {
    "project": "Hello.Native",
    "product": "Application",
    "profile": "dev"
  },
  "data": {},
  "diagnostics": []
}
```

Supported `plan` values are:

- `build`
- `stage`
- `package`
- `package-output`
- `launch`
- `runtime`
- `environment`
- `publish`
- `tooling`

The `data` field uses the same object shapes as the matching full graph
`plans` entry. Focused plan output includes provenance-bearing selected items
where the authoring and resolution layer can identify the selected source.

## Provenance

Every provenance object uses:

```json
{
  "sourceKind": "project",
  "sourceName": "Hello.Native",
  "manifestPath": "Examples/Hello.Native/Hello.Native.nginproj",
  "reason": "selected build input"
}
```

Stable V4 source kinds are:

- `convention`
- `project`
- `project-profile`
- `workspace-profile`
- `workspace-product-profile`
- `package`
- `package-feature`

Provenance explains selected graph contributions; it is not a complete audit
log of every discarded candidate. Future V4-compatible additions may include
extra provenance fields, but the four fields above remain stable for `4.x`.

## Secret Redaction

Secrets are always redacted in graph JSON, focused plans, diagnostics,
explain-style graph surfaces, and editor-facing output. A secret environment
entry must use:

```json
{
  "value": "<redacted>",
  "secret": true
}
```

Consumers must not infer the real value from `source`, `reason`, or
`manifestPath`.

## Compatibility Boundary

The freeze closes the V4 graph consumer contract. It does not mean every
future package, trust, external-provider, platform, or tooling subsystem is
implemented. Those systems may add optional fields or new plan entries under a
future minor `4.x` contract, or may introduce a new major graph schema if they
need breaking semantics.
