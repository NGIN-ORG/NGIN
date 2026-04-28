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

1. Run `Examples/App.NativeMinimal`.
2. Compare it with `Examples/App.HostedCore`.
3. Read `Examples/App.Basic` for manifest-owned runtime metadata.
4. Read `Examples/App.Showcase` for configuration overlays.
5. Read the specs below when you need exact contracts.

## Guides

- [`guides/nginproj-authoring.md`](guides/nginproj-authoring.md)
  Concise developer guide for authoring `.nginproj` files.

## Active Specs

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

## Background Material

- [`architecture/`](architecture/) contains design notes and historical
  direction documents.
- [`api-drafts/`](api-drafts/) contains draft API sketches.
- [`plans/`](plans/) contains implementation plans.
- [`reviews/`](reviews/) contains review notes.
- [`examples/`](examples/) contains older or focused documentation examples that
  are not part of the main runnable `../Examples/` tree.

When background material conflicts with an active spec, follow the active spec.
