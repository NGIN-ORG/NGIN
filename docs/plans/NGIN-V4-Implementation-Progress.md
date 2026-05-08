# NGIN V4 Implementation Progress

Status: Living Implementation Ledger

This file tracks concrete V4 implementation work. The north-star and workstream
documents describe the target product contract; this file records what the repo
actually supports today.

## Current Implementation Strategy

The first implementation slice does not yet introduce the final V4 Composition
Graph engine. Instead, it accepts V4 authoring shapes and normalizes them into
the current internal manifest structures so existing validation, build, package,
and test paths can start exercising V4 syntax.

This is intentional scaffolding:

- V4 authoring can be tested early.
- Existing CLI behavior remains available while the graph engine is built.
- The code can be replaced behind the same parser contract when the graph model
  lands.

## Implemented

### Product-First Project Parsing

`Project SchemaVersion="4"` is accepted by the CLI authoring layer.

Supported primary product elements:

- `Application`
- `Library`
- `Tool`
- `Test`
- `Benchmark`
- `Plugin`
- `Module`
- `External`

The parser enforces one primary product element per `.nginproj`.

Implemented normalization:

- product kind to current project type
- product kind and `Output` attribute to current output kind
- default profile name `dev`
- default C++ language settings
- default generated CMake backend settings
- default executable launch entry for executable products
- basic source defaults for `src`

### V4 Product Sections

Implemented product section parsing:

- `Uses`
- `Build`
- `Generate`
- `Stage`
- `Runtime`
- `Environment`
- `Launch`, partially

Supported `Uses` entries:

- `Project`
- `Package`
- `Tool`
- `Runtime`
- nested dependency `Feature`
- dependency `Scope` metadata

Supported `Build` entries:

- `Language`
- `Sources`
- `Headers`
- `IncludePath`
- `Define`
- `CompileOption`
- `LinkOption`
- `LinkLibrary`, currently normalized as a link option

Supported `Generate` entries:

- command generator identity
- package or inline tool reference
- `Args`
- typed `Inputs`
- typed `Outputs`

Generated outputs are normalized as current generated input declarations.

Supported `Stage` entries:

- `Config`
- `Content`

Supported `Runtime` entries:

- `Module`
- module `Provides Service`
- module `Requires Service`

Supported `Environment` entries:

- `Env`
- `Secret` from `local:` settings, marked secret in the current environment
  variable model

Supported product lifecycle entries:

- `Run`, normalized to the shared launch model for executable products
- `PackageOutput`, stored as source-product package output metadata
- named V4 `Launch` entries, with `Name`, `WorkingDirectory`, and `Args`
  preserved through inspect and generated `.nginlaunch`
- `Quality/Analyzer`, stored as first-pass analyzer plan metadata with name,
  scope, enabled state, severity, config path, and selectors

Supported `External` product behavior:

- `Exports/LibraryTarget` sets the exported target identity
- `Exports/IncludePath` and `Exports/Define` become interface build metadata
- generated CMake emits an imported interface target instead of treating the
  external product as a source-built library
- external project references link against the exported target

### V4 Profile Overlays

`Profile` elements are accepted as project-level overlays containing fragments
for the project's primary product kind.

Implemented profile behavior:

- profile `Name`
- profile `Extends`
- profile `Defaults`
- separate `HostPlatform` profile metadata
- product-specific `Build`, `Stage`, `Runtime`, `Environment`, and `Launch`
  fragments
- selected profile environment materialization
- profile selectors on V4 build settings and staged inputs
- staged output override with `Collision="Override"`
- duplicate staged output diagnostics when multiple V4 stage entries target the
  same destination without an explicit override
- environment variable replacement by name

This is still not the final V4 overlay engine. Build definitions, staged
inputs, environment variables, and runtime modules have first-pass identity
handling, but package feature overlays, dependency removals, publish overlays,
and full provenance are not implemented.

### V4 Workspace Parsing

`Workspace SchemaVersion="4"` is accepted.

Implemented workspace parsing:

- workspace `Name`
- workspace `DefaultProfile`
- `Imports/Import Path`
- imported `Definitions SchemaVersion="4"` fragments
- definition `Platforms/Platform`
- definition `Toolchains/Toolchain`
- `Projects/Project Path`
- `Packages/Source Path`
- `Packages/Source Url`
- `Packages/Version Name Range`
- package provider roots via `Packages/PackageProvider`
- workspace `Defaults` for build type, host platform, target platform,
  toolchain, environment, language, and backend
- workspace `Profiles/Profile/Defaults` for build type, host platform, target
  platform, toolchain, and environment

Workspace `DefaultProfile` participates in command profile selection when no
explicit `--profile` is passed. If the selected project does not declare a
matching local profile, V4 can synthesize the selected profile from workspace
policy for projects without local profile overlays.

Workspace language and backend defaults apply to projects that have not declared
explicit project build settings. Project-local product `Build` settings remain
stronger than workspace defaults.

Platform definition `Abi` values and selected toolchain definitions flow into
the resolved launch model and inspect selection payload as the first
platform/toolchain resolution surface.

Full workspace profile product overlays, remote package URLs, and
definition-driven project resolution beyond first-pass profile policy are still
future work.

### V4 Package Manifest Parsing

`Package SchemaVersion="4"` is accepted.

Implemented package parsing:

- package `Name`
- package `Version`
- package-level `Uses`
- `Library/Exports/Headers`
- `Library/Exports/Binary`
- `Library/Exports/LibraryTarget`
- `Tool/Exports/Tool`
- `Features/Feature`
- feature `Uses` dependencies
- feature `Build/Define`
- feature `Provides/Capability`
- feature dependency `Scope` metadata

The parser treats `.nginpkg` as reusable package contract metadata, not a source
build recipe.

### Dependency Scope Metadata

Dependency scope is now represented in the current package reference model.
During resolution, package references are still identified by dependency name,
and repeated declarations merge scope metadata instead of treating scope as a
separate dependency identity.

Resolved package scopes are carried into:

- inspect package JSON
- package lock output
- package graph plan output
- package object explanation
- profile diff package values

Inspect package JSON also includes first-pass closure classification derived
from scope metadata:

- `Build` -> `Host`
- `Target` -> `Target`
- `Runtime` -> `Runtime`
- `Test` -> `Test`
- `Dev` -> `Dev`
- `Publish` -> `Publish`

This matches the V4 design rule:

```text
Dependency identity = kind + name.
Dependency scope = mergeable metadata.
```

V4 bracket version ranges such as `[1.0.0,2.0.0)` are accepted during package
resolution in addition to the older comparator form.

Package version conflict handling is now first-pass graph policy:

- repeated selected declarations for the same package identity merge scopes
- repeated selected declarations with incompatible explicit ranges are reported
  as diagnostics
- transitive dependencies that request a later incompatible range for an
  already selected package are reported as diagnostics
- workspace `PackageProvider` source overrides are surfaced in resolved package
  metadata, inspect JSON, lock output, and backend package integration

### Inspect Product Identity

`ngin inspect --format json` now emits a `product` object with:

- `kind`
- `outputType`
- `outputName`
- `targetName`

This is not the final Composition Graph JSON schema, but it begins moving the
inspect contract toward V4's product-first vocabulary.

Inspect profile selection also emits `hostPlatform` separately from target
`platform`.

Inspect launch output emits the selected launch name and launch arguments.

Inspect JSON also emits a first-pass `compositionGraph` object with V4 graph
schema version, resolved state, facet names, selected identity, selected
profile/platform context, and resolved facet summary counts. This is a
migration surface, not the final graph JSON schema.

Inspect JSON includes first-pass quality analyzer metadata under
`quality.analyzers` so editor and tooling consumers can see V4 analyzer plans
without reparsing manifests.

### New Project Command

`ngin new` can create V4 product-first project skeletons.

Supported forms:

```bash
ngin new app Hello.Native
ngin new lib Game.Engine
ngin new tool Game.AssetCompiler
ngin new test Game.Engine.Tests
ngin new benchmark Game.Engine.Benchmarks
ngin new plugin Game.AdminPlugin
```

Generated projects use:

```xml
<Project SchemaVersion="4" Name="...">
  <Application />
</Project>
```

or the matching product element for the requested kind.

### Package Editing Commands

`ngin package add`, `ngin package update`, and `ngin package remove` can edit
V4 product-first projects.

Supported form:

```bash
ngin package add NGIN.Core --version "[0.1.0,0.2.0)" --scope "Target;Runtime"
ngin package update NGIN.Core --version "[0.2.0,0.3.0)" --scope Build
ngin package remove NGIN.Core
ngin package pack
```

Current behavior:

- supports V4 product-first `.nginproj` files
- converts self-closing product elements such as `<Application />` into an
  expanded product with a `Uses` section
- inserts a `Uses/Package` dependency
- updates a `Uses/Package` dependency version and scope
- removes a `Uses/Package` dependency
- preserves dependency scope metadata
- rejects duplicate package references by name

### Restore Command

`ngin restore` is available as the first concrete package acquisition command.

Current behavior:

- resolves the selected project/profile package closure
- materializes package manifests or phase-one `.nginpack` archives into a local
  package store
- default store path is `.ngin/packages` under the workspace or project root
- `--output` can select a package store directory
- writes the deterministic `ngin.lock`
- `--locked` verifies the existing lock file before restoring and refuses graph
  drift
- preserves resolved dependency scopes in the lock file

This is local manifest/package-store scaffolding. Network feeds, hashes, final
compressed archive extraction, and source/binary provider switching are still
future work.

### Package Sources Command

`ngin package sources list`, `ngin package sources add`, and
`ngin package sources remove` cover the first V4 workspace package source
management workflow.

Current behavior:

- reads the selected workspace
- prints local package source paths
- prints remote package source URLs
- prints package provider roots
- marks missing local paths
- adds a local package source under `Workspace/Packages`
- creates the `Packages` section when a V4 workspace does not yet have one
- writes URL sources with `Url` and local sources with `Path`
- removes a named workspace package source

`file://` package source URLs participate in package catalog resolution as
phase-one static feed roots.

### Graph Diff Command

`ngin diff` can compare two resolved profiles for a project:

```bash
ngin diff \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --from-profile dev \
  --to-profile shipping
```

Current diff coverage:

- profile selection values
- compile definitions
- packages
- package features
- generators
- generated outputs
- staged files
- runtime modules
- plugins
- environment variables, with secret redaction preserved
- launch name, executable, working directory, and args
- resolved artifacts

This is not the final graph diff engine, but it is the first product-facing
implementation of the V4 graph change-review workflow.

### Object-Identity Explain

`ngin explain <kind>:<identity>` is implemented for the resolved objects the
current resolver can answer.

Supported object identities:

- `property:Language`
- `property:BuildType`
- `property:HostPlatform`
- `property:TargetPlatform`
- `property:Toolchain`
- `property:Environment`
- `define:<Name>`
- `stage:<relative-path>`
- `package:<Name>`
- `feature:<Package>/<Feature>`
- `generator:<Name>`
- `launch:<Name>`
- `runtime-module:<Name>`

This coexists with the older focused explain subcommands while V4 moves toward
object identity syntax.

### Focused Graph Plans

`ngin graph` accepts V4 north-star plan switches:

```bash
ngin graph --build-plan
ngin graph --stage-plan
ngin graph --package-plan
ngin graph --launch-plan
ngin graph --runtime-plan
ngin graph --publish-plan
ngin graph --quality-plan
```

Current behavior is text output over the existing resolved model. This is not
the final graph API, but it exposes focused plan views for build inputs,
staged files, package closure, launch metadata, runtime selection, publish
declarations, and quality analyzer declarations.

### Test And Benchmark Lifecycle Commands

`ngin test` and `ngin benchmark` are available for V4 product-first projects.

Current behavior:

- `ngin test` requires a V4 `Test` product
- `ngin benchmark` requires a V4 `Benchmark` product
- both commands reuse the same build/stage/launch path as `ngin run`
- product `Run Args` are split and passed to the executable
- additional CLI args after `--` are appended after manifest args

### Analyze Command

`ngin analyze` is available as the first V4 quality workflow command.

Current behavior:

- resolves the selected project/profile
- reads V4 `Quality/Analyzer` declarations
- applies analyzer selectors and enabled flags
- reports the analyzer plan with scope, severity, and config path

This is not yet a full analyzer runner. Invoking tools such as `clang-tidy`,
coverage collection, formatter enforcement, and quality policy execution are
still future work.

### Stage Command

`ngin stage` is available as a public lifecycle command.

Current behavior:

- reuses the current build/stage pipeline
- writes the same staged output layout as `ngin build`
- reports output directory, launch manifest, and selected executable
- validates V4 staged content through the existing stage collision and copy
  path

### Publish Model And Command

V4 product `Publish` declarations are parsed into the project/profile model.

Implemented publish behavior:

- product-level `Publish`
- profile overlay `Publish`
- publish identity by `Name`
- `Kind="Folder"`
- `Output`
- first-pass `Include` metadata for stage, runtime dependencies, and symbols
- `ngin publish [PublishName]`
- inspect JSON `publishes`
- graph `--publish-plan`
- profile diff publish entries

The current publish command builds and stages the selected product, then copies
the staged layout to the folder publish output. Folder publish removes stale
output before copying the new staged layout. Archive publish, signing, SBOM,
trust policy, and `.nginpack` integration are still future work.

### Repository Example Migration

`Examples/Hello.Native/Hello.Native.nginproj` has been migrated to
`SchemaVersion="4"` using the product-first `Application` shape.

`Examples/Hello.Hosted/Hello.Hosted.nginproj` has been migrated to
`SchemaVersion="4"` using:

- `Application`
- `Uses/Runtime`
- `Build/Define`
- `Stage/Config`
- `Runtime/Module`
- product-level `Launch`

`Examples/Hello.Reflection/Hello.Reflection.nginproj` has been migrated to
`SchemaVersion="4"` using nested package feature selection under `Uses`.

Verified:

```bash
.ai/skills/test-cpp/scripts/test.sh hello-native
.ai/skills/test-cpp/scripts/test.sh hello-hosted
.ai/skills/test-cpp/scripts/test.sh hello-reflection
.ai/skills/test-cpp/scripts/test.sh ngin-core
.ai/skills/test-cpp/scripts/test.sh workspace
```

### NGIN.Core V4 Runtime Reader

`NGIN.Core` can now read the V4 project shape needed by `Hello.Hosted` at
runtime. This is a minimal runtime-side reader, not the full CLI graph resolver.

### Launch Arguments

Launch declarations now preserve `Args` in the shared launch model.

Implemented flow:

- project and profile launch parsing
- V4 `Run` parsing
- V4 product launch parsing
- inspect JSON output
- generated `.nginlaunch` metadata

### Package Output Metadata

Source projects can now parse V4 `PackageOutput` declarations into the current
project manifest model.

Implemented metadata:

- output package name and version
- source product identity
- description
- license
- exported headers, libraries, tools, and capabilities
- target platform list
- ABI tag string

`ngin inspect --format json` emits a first-pass `packageOutputs` array.

`ngin package pack` can emit a standalone V4 `.nginpkg` manifest and a
first-pass deterministic `.nginpack` artifact from a source project
`PackageOutput` declaration.

Supported generated package metadata:

- package name and version
- description
- license
- exported headers
- exported library targets
- exported tools
- exported capabilities
- target platforms
- ABI tag

The current `.nginpack` format is a deterministic phase-one artifact containing
archive metadata and the embedded package manifest. Package cataloging,
manifest loading, and restore can consume this phase-one archive. It is not yet
the final compressed archive/store format.

### V4 Conditions And `When`

V4 projects can parse root `Conditions` using `<When>` as the product-first
selector node spelling.

Implemented support:

- `<When>` as an alias for structured condition matches
- root project condition parsing for `SchemaVersion="4"`
- `When="condition-name"` selectors on V4 build settings

## Verified

The CLI target builds successfully:

```bash
.ai/skills/build-cpp/scripts/build.sh cli
```

The workspace test suite passes:

```bash
.ai/skills/test-cpp/scripts/test.sh workspace
```

Current test coverage includes:

- V4 minimal application project normalization
- V4 minimal library project normalization
- V4 external product imported interface target generation
- V4 first-pass inspect `compositionGraph` marker
- V4 inspect `compositionGraph` identity, selection, and facet summary payload
- V4 `ngin new`
- V4 `ngin package add`
- V4 `ngin package update`
- V4 `ngin package remove`
- V4 `ngin package pack`
- V4 `ngin restore`
- V4 restore from phase-one `.nginpack` package archives
- V4 locked restore enforcement
- V4 package catalog resolution from `file://` package source URLs
- V4 `ngin package sources list`
- V4 `ngin package sources add`
- V4 `ngin package sources remove`
- V4 `ngin test`
- V4 `ngin benchmark`
- V4 `ngin analyze`
- V4 `ngin stage`
- V4 `ngin publish` for folder publishes
- V4 folder publish stale output cleanup
- V4 test product dependencies
- V4 hosted application runtime/package/stage/environment/module parsing
- V4 workspace project/source/version parsing
- V4 workspace default profile command selection
- V4 workspace profile policy on projects without local profiles
- V4 workspace language/backend build defaults
- V4 platform definition ABI tag resolution into inspect selection
- V4 selected toolchain definition resolution into inspect selection
- V4 package export and feature parsing
- V4 source package provider override metadata
- V4 profile-to-profile diff
- V4 object-identity explain
- V4 focused graph plan switches
- V4 quality graph plan switch
- V4 publish graph/inspect/diff surface
- V4 named launch metadata
- V4 bracket package version range resolution
- V4 stage identity collision diagnostics
- V4 inspect package closure classification from dependency scopes
- resolved package scope metadata

## Not Implemented Yet

The following are still open and should not be described as complete:

- final V4 Composition Graph data model
- graph JSON contract
- named convention graph contributions and provenance records
- real V4 overlay identity, remove, override, and duplicate diagnostics
- full host/target dependency closure separation during restore/build
- full workspace profile product overlays, imports, and definitions
- full platform/toolchain compatibility semantics
- full ABI tag compatibility matching for binary package selection
- network package restore from V4 package sources
- full external dependency adapters for system, CMake package, pkg-config,
  vcpkg, and Conan providers
- archive publish, signing, SBOM, trust policy
- final compressed `.nginpack` archive packing and extraction
- full analyzer execution, formatter execution, coverage collection, and
  quality policy enforcement
- final graph diff engine over a stable graph JSON schema
- V4 editor schema/completion metadata

## Next Iteration

The next implementation slice should focus on one of these paths:

- replace the first-pass inspect payload with the stable Composition Graph JSON
  schema
- expand workspace profile product overlays beyond defaults
- deepen V4 overlay identity/remove/override semantics beyond the current
  first-pass item identities
- migrate remaining examples and docs to V4 syntax
- implement network package restore and feed index resolution
- implement final compressed `.nginpack` archive extraction
