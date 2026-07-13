# Documentation

This directory contains active specs, architecture notes, plans, reviews, API
drafts, and older design drafts. Use the specs as the source of truth for active
behavior. Treat architecture notes and drafts as background unless a current spec
points to them.

For the shortest path into the repo, start outside this directory:

- [`../README.md`](../README.md) for the first-run flow
- [`../Examples/README.md`](../Examples/README.md) for runnable examples
- [`../Tools/README.md`](../Tools/README.md) for CLI and editor tooling

## Learning Path

If you are trying to understand NGIN as a user:

1. Run `Examples/Hello.Native`.
2. Compare it with `Examples/Hello.Hosted`.
3. Run `Examples/Hello.Reflection` when you need generator-backed reflection.
4. Run `Examples/Hello.Analyzer` when you need package-provided tool execution.
5. Run `Examples/Hello.Formatter` when you need edit-producing format actions.
6. Read the specs below when you need exact contracts.

## Guides

- [`guides/nginproj-authoring.md`](guides/nginproj-authoring.md)
  Concise developer guide for authoring `.nginproj` files.
- [`guides/tool-driver-authoring.md`](guides/tool-driver-authoring.md)
  Package author guide for general tools, drivers, actions, probes, and events.

## Active Specs

- [`specs/014-tooling-and-quality-execution.md`](specs/014-tooling-and-quality-execution.md)
  Active V4 tooling authoring, driver protocol, policy, CLI, and editor contract.

- [`specs/001-core-concepts.md`](specs/001-core-concepts.md)  
  Shared vocabulary: project, configuration, composition, package, workspace,
  and launch manifest.
- [`specs/002-project-and-target-manifest.md`](specs/002-project-and-target-manifest.md)  
  `.nginproj` file contract.
- [`specs/003-package-manifest-and-runtime-contributions.md`](specs/003-package-manifest-and-runtime-contributions.md)  
  `.nginpkg` file contract and package contribution model.
- [`specs/004-composition-and-validation.md`](specs/004-composition-and-validation.md)  
  Composition resolution and validation expectations.
- [`specs/005-staged-target-manifest.md`](specs/005-staged-target-manifest.md)  
  Generated `.nginlaunch` role.
- [`specs/006-cli-contract.md`](specs/006-cli-contract.md)  
  Active `ngin` command surface.
- [`specs/007-host-integration-contract.md`](specs/007-host-integration-contract.md)  
  How `NGIN.Core` relates to the authored model.
- [`specs/008-roadmap-and-non-goals.md`](specs/008-roadmap-and-non-goals.md)  
  Current direction and explicit non-goals.
- [`specs/009-package-distribution-and-installation.md`](specs/009-package-distribution-and-installation.md)  
  Planned package distribution and installed-mode behavior.
- [`specs/010-workspace-and-project-model.md`](specs/010-workspace-and-project-model.md)  
  Workspace/project/package split.
- [`specs/011-workspace-manifest.md`](specs/011-workspace-manifest.md)  
  `.ngin` workspace file contract.
- [`specs/012-tooling-and-runtime-boundary.md`](specs/012-tooling-and-runtime-boundary.md)  
  Boundary between NGIN tooling and the optional hosted runtime.
- [`specs/013-composition-graph-json-contract.md`](specs/013-composition-graph-json-contract.md)
  Frozen V4 Composition Graph JSON contract for CLI and editor consumers.

## Machine-Readable Schemas

- [`schemas/ngin-composition-graph-v4.schema.json`](schemas/ngin-composition-graph-v4.schema.json)
  JSON Schema for `NGIN.CompositionGraph` and `NGIN.CompositionGraphPlan`.
- [`schemas/ngin-tool-driver-v1.schema.json`](schemas/ngin-tool-driver-v1.schema.json)
  Request and JSONL event schema for `NGIN.ToolDriver/1`.
- [`schemas/ngin-tool-result-v1.schema.json`](schemas/ngin-tool-result-v1.schema.json)
  Normalized diagnostics, edits, artifacts, metrics, execution, and gate result
  envelope for tool runs.
- [`schemas/ngin-tool-baseline-v1.schema.json`](schemas/ngin-tool-baseline-v1.schema.json)
  Fingerprint baseline schema for new-findings-only quality gates.

## Active Implementation Plans

- [`plans/NGIN-General-Tooling-And-Quality-Execution-Plan.md`](plans/NGIN-General-Tooling-And-Quality-Execution-Plan.md)
  Breaking post-phase-one plan for the general tool driver, execution,
  diagnostics, quality policy, report, and editor framework that replaces the
  current clang-tidy-specific analyzer runner.

## Background Material

- [`architecture/`](architecture/) contains design notes and historical
  direction documents.
- [`api-drafts/`](api-drafts/) contains draft API sketches.
- [`plans/`](plans/) contains implementation plans.
- [`reviews/`](reviews/) contains review notes.
- [`examples/`](examples/) contains older or focused documentation examples that
  are not part of the main runnable `../Examples/` tree.

When background material conflicts with an active spec, follow the active spec.
