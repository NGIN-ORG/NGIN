# NGIN V4 Product-First Implementation Workstreams

Status: Strategic Work Breakdown

## Purpose

This document turns `NGIN-V4-North-Star-And-Subsystem-Plans.md` into concrete
workstreams for the fully breaking V4 rewrite.

V4 is product-first and graph-native:

```text
One .nginproj describes one primary product identity.
The workspace groups related products.
The Composition Graph is the source of truth.
```

This is not an active spec. It is the implementation planning map for the V4
contract.

## Workstream A: NGIN Composition Graph v1

### Goal

Create the V4 product contract. Project XML, package XML, workspace XML,
definition imports, generated metadata, and future authoring frontends all feed
or consume the same graph.

### Scope

- graph data model
- workspace, project, product, and profile nodes
- product kinds: `Application`, `Library`, `Tool`, `Test`, `Benchmark`,
  `Plugin`, `Module`, `External`
- phase-one decision point for standalone `Module` products versus runtime
  module declarations inside other products
- separate graph roles for external products and external dependency providers
- selected project/profile/platform context
- host platform, target platform, runtime platform, package platform
- package closure
- dependency scopes
- dependency identity as kind plus name, with scope as mergeable metadata
- source/header/content/config/assets
- build settings
- generator declarations
- staged outputs
- runtime metadata
- environment, launch, test, benchmark, publish metadata
- diagnostics
- provenance
- named conventions
- graph facets
- graph diff inputs

### Key Decisions

- graph node identity rules
- product identity rules
- graph facet model for build, generate, stage, runtime, environment, launch,
  test, benchmark, publish, package, editor, trust, diagnostics, provenance
- provenance storage shape
- stable JSON schema versioning
- difference between unresolved, resolved, and built-known graph data
- graph diff input/output model
- package resolution policy and selected-version provenance
- secret redaction in normal graph, diagnostic, launch, explain, and diff output

### First Acceptance Gate

`ngin inspect --format json` can emit a graph-shaped payload for a V4-native
single-product project.

## Workstream B: Product-First V4 Project Parser

### Goal

Make `.nginproj` describe one primary product identity with a product element
that owns the product lifecycle.

### Scope

- `Project SchemaVersion="4"`
- project root `Name`
- exactly one primary product element
- product elements: `Application`, `Library`, `Tool`, `Test`, `Benchmark`,
  `Plugin`, `Module`, `External`
- product-specific allowed sections
- `DefaultProfile`
- product-intent profiles such as `dev`, `test`, `ci`, and `shipping`
- product-aware profile overlays
- repository examples rewritten to V4-native shape

### First Acceptance Gate

This project validates, builds, stages, and runs:

```xml
<Project SchemaVersion="4" Name="Hello.Native">
  <Application />
</Project>
```

### Second Acceptance Gate

This library validates and emits a graph with library defaults and no launch
entry:

```xml
<Project SchemaVersion="4" Name="Game.Engine">
  <Library />
</Project>
```

### Third Acceptance Gate

A test product can reference a library project and run through `ngin test`:

```xml
<Project SchemaVersion="4" Name="Game.Engine.Tests">
  <Test>
    <Uses>
      <Project Name="Game.Engine"
               Path="../Game.Engine/Game.Engine.nginproj" />
    </Uses>
  </Test>
</Project>
```

## Workstream C: Product Sections And Named Conventions

### Goal

Define the default graph contributions and allowed lifecycle sections for each
product kind.

### Scope

- product default conventions
- `NGIN.Application`
- `NGIN.Library`
- `NGIN.Tool`
- `NGIN.Test`
- `NGIN.Benchmark`
- `NGIN.Plugin`
- `NGIN.Module`
- `NGIN.External`
- `NGIN.Cpp.Defaults`
- `NGIN.Profile.dev`
- `NGIN.HostPlatform`
- `NGIN.CMake.Generated`
- product section validation
- `ngin explain` provenance for defaults

### First Acceptance Gate

`ngin explain property:Language` on a minimal app shows `C++23`/`CXX 23` coming
from `NGIN.Cpp.Defaults`, with an override hint.

### Second Acceptance Gate

The parser rejects product sections that do not belong to the selected product
kind, unless explicitly allowed by the V4 contract.

## Workstream D: Overlay, Identity, And Provenance

### Goal

Make graph fragment merging deterministic before packages, profiles, generators,
and staging depend on it.

### Scope

- scalar property precedence
- product-aware profile overlays
- item identity keys by item type
- append/remove/override semantics
- duplicate diagnostics
- condition and selector provenance
- named default provenance
- stage collision policy
- `Env`, `LaunchEnv`, `RuntimeSetting`, and `Secret` semantics
- secret redaction semantics
- feature, capability, and `Uses` semantics
- dependency scope merge rules

### First Acceptance Gate

A `shipping` profile can override inherited defines, staged config files,
runtime modules, package features, and launch arguments with deterministic
diagnostics and explainable provenance.

### Second Acceptance Gate

Stage path collisions are errors by default and become valid only when the
author declares an explicit collision or merge policy.

### Third Acceptance Gate

Re-declaring the same dependency with compatible scopes merges into one graph
dependency identity. Conflicting versions or providers produce diagnostics.

## Workstream E: Workspace And Definition Imports

### Goal

Make the workspace the repository/product-family policy layer without turning it
into a project file.

### Scope

- `Workspace SchemaVersion="4"`
- workspace project grouping
- `Definitions SchemaVersion="4"`
- workspace `Import`
- shared defaults
- central package versions
- package providers
- package sources
- toolchain policy
- platform declarations
- profile templates
- workspace-level quality and trust policy

### First Acceptance Gate

The root `NGIN.ngin` can express V4 workspace defaults, project list, imported
definitions, package versions, and local package providers.

### Second Acceptance Gate

Imported `.ngin.xml` definition fragments contribute platforms and toolchains to
the graph with provenance.

## Workstream F: Local Package Graph And Package Manifest V4

### Goal

Separate reusable package consumption from source build behavior and make local
package resolution part of graph construction.

### Scope

- `.nginpkg` package identity
- package metadata
- package exports
- package features
- package dependencies
- dependency scopes
- dependency identity as kind plus name
- scope, feature, version, and provider merge rules
- package tools and generators
- runtime modules and plugins
- capabilities
- content/config/assets
- package policy
- `PackageOutput` inside source products
- local source providers
- system/external adapter provider shape
- package resolution policy
- host-tool versus target-package closure
- package manifests rewritten to V4-native shape

### First Acceptance Gate

`NGIN.Core.nginpkg` can be expressed as a V4 package with explicit exports and
features while local source build behavior remains owned by project/provider
metadata.

### Second Acceptance Gate

A build-scope tool package resolves for the host platform while target/runtime
packages resolve for the target platform.

### Third Acceptance Gate

A source library product can declare `PackageOutput` without confusing it with
package dependencies consumed through `Uses`.

### Fourth Acceptance Gate

The graph and lock file record selected package version, provider, source/hash,
selected features, compatibility match, dependency scopes, and provenance.

## Workstream G: Restore, Lock Files, And Package Store

### Goal

Make package acquisition deterministic and reviewable after the local package
graph works.

### Scope

- local package store
- immutable `.nginpack` archive layout
- static package feed format
- source package provider selection
- binary package provider selection
- lock file
- restore command
- locked restore
- package source commands
- package add/remove/update commands
- package trust metadata placeholders

### First Acceptance Gate

A project can restore `NGIN.Core` from a local feed into a package store, write a
lock file, and build from the restored package without scanning
repository-local package folders.

### Later Acceptance Gate

The same project can switch to a source provider override from the workspace
without changing the product dependency declaration.

## Workstream H: Backend-Neutral Build Plan And CMake Emitter

### Goal

Make CMake generation an output of the Composition Graph, not a second source of
truth.

### Scope

- build plan data model
- output type selection per product kind
- source sets
- generated source integration
- compile definitions
- include paths
- compile/link options
- precompiled headers
- unity build setting
- target dependencies
- host tool dependencies
- package exports
- external build adapters
- CMake emitter
- compile database discovery

### First Acceptance Gate

Generated CMake behavior can be emitted from the graph-backed build plan for
minimal `Application` and `Library` projects.

### Second Acceptance Gate

A `Tool` product builds as an executable and can be used as a `Build`-scope
dependency by another product.

## Workstream I: Generators And Pipeline Phases

### Goal

Make generated files and command tools hermetic, host/target aware, explainable,
and reusable.

### Scope

- phase model
- tool declarations
- generator context
- command execution contract
- declared inputs
- declared outputs
- generated output validation
- generated outputs as typed graph inputs
- package-contributed generators
- project tool products used as generators
- host-platform tool execution
- target-platform generator context
- explain generator

### First Acceptance Gate

A product builds using a `Build`-scope package-provided generator that consumes a
graph-generated context file and contributes declared generated sources.

### Second Acceptance Gate

A product builds using a `Tool` project from the same workspace as a generator.

## Workstream J: Staging, Launch, Runtime, And Publish

### Goal

Keep staged output as a first-class graph product and separate local launch from
publish output.

### Scope

- stage plan
- stage path collision diagnostics
- stage collision policy
- runtime dependency staging
- config/content/assets/plugins
- generated launch metadata
- multiple launch entries
- runtime graph
- runtime settings
- publish profiles
- app bundle/package output

### First Acceptance Gate

`ngin graph --stage-plan` explains the selected product's stage layout before
`ngin run` consumes `.nginlaunch`.

### Second Acceptance Gate

A hosted `Application` product stages config, runtime content, plugin metadata,
and runtime settings without requiring the shipped app to load source manifests.

## Workstream K: Tests, Benchmarks, Analysis, And CI

### Goal

Make quality workflows product-aware first-class graph consumers.

### Scope

- `Test` product declarations
- `Benchmark` product declarations
- test run declarations
- benchmark run declarations
- reports
- analyzer declarations
- warning policy
- sanitizer policy
- coverage policy
- CI profile templates
- command surface

### First Acceptance Gate

`ngin test --project <file> --profile <name>` runs a V4 `Test` product with the
same resolved package/build/stage context as `ngin build`.

### Second Acceptance Gate

`ngin benchmark --project <file> --profile <name>` runs a V4 `Benchmark` product
and writes a declared report.

## Workstream L: CLI Product Layer

### Goal

Make NGIN feel like one coherent project tool instead of a collection of backend
wrappers.

### Scope

- `ngin new app`
- `ngin new lib`
- `ngin new tool`
- `ngin new test`
- `ngin new benchmark`
- `ngin new plugin`
- `ngin add package`
- `ngin add project-reference`
- `ngin restore`
- `ngin configure`
- `ngin build`
- `ngin stage`
- `ngin run`
- `ngin test`
- `ngin benchmark`
- `ngin analyze`
- `ngin format`
- `ngin diff`
- `ngin publish`
- object-identity `ngin explain`
- polished diagnostics

### First Acceptance Gate

A new user can create, build, run, and add a package without manually editing
CMake:

```bash
ngin new app Hello.Native
cd Hello.Native
ngin build
ngin run
ngin package add NGIN.Core
```

### Second Acceptance Gate

`ngin new test Game.Engine.Tests --uses Game.Engine` creates a V4 `Test` product
that can be run through `ngin test`.

## Workstream M: Graph Diff And Change Review

### Goal

Make graph changes reviewable across profiles, lock files, package updates, and
manifest edits.

### Scope

- profile-to-profile graph diff
- lock-file diff
- package closure diff
- feature selection diff
- stage plan diff
- launch plan diff
- runtime graph diff
- build setting diff
- publish plan diff
- human-readable summaries
- machine-readable diff output

### First Acceptance Gate

`ngin diff --from-profile dev --to-profile shipping` explains changed build
type, optimization, defines, package features, staged files, launch entries,
runtime modules, and publish outputs.

## Workstream N: Editor And Schema Tooling

### Goal

Make editor integration consume the same graph and schema as the CLI.

### Scope

- inspect JSON contract
- schema metadata
- completion metadata
- formatter
- snippets/templates
- VS Code extension parser reduction
- compile database location
- generated file awareness
- test, benchmark, and launch UI
- package graph UI
- graph diagnostics UI

### First Acceptance Gate

The VS Code extension can list product kind, profiles, diagnostics, launch
entries, test runs, and compile database information by consuming CLI inspect
output rather than replicating manifest resolution rules.

## Workstream O: Documentation And Examples

### Goal

Teach V4 from tiny native products through advanced package/runtime workflows.

### Scope

- README update
- authoring guide
- product guide
- package guide
- workspace guide
- repository conversion notes
- before/after design examples where they clarify V4 intent
- command cookbook
- troubleshooting guide

### Example Ladder

Use an intentionally progressive ladder:

- `Hello.Native`: almost empty `Application`
- `Hello.Hosted`: small hosted runtime `Application`
- `Hello.Reflection`: hosted `Application` with package tool and generated code
- `Game.Engine`: reusable `Library`
- `Game.AssetCompiler`: `Tool`
- `Game.AdminPlugin`: `Plugin`
- `Game.Engine.Tests`: `Test`
- `Game.Engine.Benchmarks`: `Benchmark`
- `Game.Client` and `Game.Server`: multi-project workspace applications

### First Acceptance Gate

A reader can understand why NGIN exists, create a tiny application, add a
package, run tests, inspect the graph, and understand one-project-one-product
without reading every spec.

## Recommended Build Order

1. Composition Graph skeleton.
2. Product-first V4 `.nginproj` parser.
3. Named conventions and default provenance.
4. Overlay, identity, remove, and override semantics.
5. Minimal workspace project discovery.
6. Definition imports and workspace policy.
7. Local package graph and V4 `.nginpkg`.
8. Backend-neutral build plan.
9. CMake emitter from build plan.
10. Generator context and hermetic generation.
11. Stage plan and launch plan.
12. Restore, lock file, and local package store.
13. Graph diff and explain commands.
14. Tests, benchmarks, analyzers, and publish.
15. VS Code consumes graph JSON.
16. Docs and example ladder.

## Risk Register

### Product Syntax Before Graph

Risk: V4 becomes a prettier XML schema without a better internal product graph.

Mitigation: build the graph/provenance model first and make the product-first
parser feed it.

### Product Kind Sprawl

Risk: product kinds become a taxonomy fight.

Mitigation: each `.nginproj` still has one primary product identity, and product
kinds are lifecycle conventions with explicit graph normalization.

### Multi-Product Pressure

Risk: users want to put a whole solution into one `.nginproj`.

Mitigation: keep one-project-one-product firm. The workspace groups related
products and owns product-family policy.

### Package Ambition Too Early

Risk: remote package work blocks manifest and build graph progress.

Mitigation: build the local package graph before package acquisition, then add
package store, lock files, static feeds, and remote trust in later phases.

### CMake Leakage

Risk: users still need to think in CMake for common workflows.

Mitigation: model warnings, toolchains, generated files, tests, staging, publish,
and package outputs as NGIN graph concepts with backend-specific escape hatches.

### Hidden Defaults

Risk: tiny manifests feel magical and untrustworthy.

Mitigation: every default is a named convention and is visible through `graph`
and `explain`.

### Runtime Coupling

Risk: hosted runtime concepts leak into plain native projects.

Mitigation: runtime participation is item/section-driven through `Runtime`,
runtime modules, runtime settings, runtime dependencies, and plugins. Plain
native products remain first-class.

### Editor Divergence

Risk: VS Code and CLI disagree on product interpretation.

Mitigation: editor tooling consumes CLI graph output.

## Definition Of Done For V4

V4 is not done when the parser accepts `SchemaVersion="4"`. V4 is done when:

- one `.nginproj` defines exactly one primary product identity
- minimal native applications are genuinely tiny
- libraries, tools, tests, benchmarks, plugins, modules, and external products
  have clear lifecycle sections
- advanced products remain explicit and inspectable
- workspace policy groups related products without stealing product identity
- packages restore deterministically
- source and binary package modes share one dependency declaration
- `PackageOutput` is distinct from consumed packages
- build, target, runtime, test, dev, and publish dependency scopes are modeled
- host and target package closures are separate
- CMake generation comes from the Composition Graph
- generated files are hermetic and declared
- staged output is collision-aware and explainable
- graph diff makes profile and package changes reviewable
- tests, benchmarks, analyzers, and publish are part of normal workflow
- editor tooling consumes the same graph as the CLI
- active docs teach V4 as the normal path
