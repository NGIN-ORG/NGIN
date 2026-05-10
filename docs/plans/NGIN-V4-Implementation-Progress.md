# NGIN V4 Implementation Progress

Status: Living Implementation Ledger

This file tracks concrete V4 implementation work. The north-star and workstream
documents describe the target product contract; this file records what the repo
actually supports today.

## Current Implementation Strategy

V4 is now treated as the only supported CLI authoring schema for projects,
workspaces, packages, and definition fragments. V1/V2/V3 project, workspace,
package, and `.nginmodel` compatibility paths have been removed from the active
CLI authoring path instead of being kept behind unreachable branches.

The implementation still uses current resolver/build structures under the
Composition Graph facade where that is pragmatic, but the public authoring
contract is V4-only:

- `.nginproj` must use `SchemaVersion="4"` and product-first shape.
- `.ngin` must use `Workspace SchemaVersion="4"`.
- `.nginpkg` must use `Package SchemaVersion="4"`.
- `.nginmodel` is no longer part of the active V4 workspace model.
- `inspect --format json` and `graph --format json` emit the V4 Composition
  Graph envelope directly.

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
- product-specific `Uses` fragments for profile-selected project/package
  references and package features
- `Uses/Project Remove="..."` for profile-selected project reference removal
- `Uses/Package Remove="..."` for profile-selected package dependency removal
- selected profile environment materialization
- profile selectors on V4 build settings and staged inputs
- profile selectors on V4 dependency and package feature uses
- staged output override with `Collision="Override"`
- duplicate staged output diagnostics when multiple V4 stage entries target the
  same destination without an explicit override
- same-scope duplicate identity diagnostics for generators, publishes, package
  outputs, runtime modules, and environment variables
- environment variable replacement by name
- generator replacement and removal by `Name` in selected profile overlays
- publish replacement and removal by `Name` in selected profile overlays
- package output replacement and removal by `Name` in selected profile overlays

This is still not the final V4 overlay engine. Build definitions, staged
inputs, dependency features, project/package removals, environment variables,
runtime modules, generators, publishes, and package outputs have first-pass
identity handling, but full provenance and duplicate diagnostics are not
implemented for every item family.

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
- workspace `Profiles/Profile/Build` for first-pass profile-selected include
  paths, defines, compile options, and link options
- workspace `Profiles/Profile/Quality/Analyzer` for first-pass
  profile-selected analyzer policy
- workspace `Profiles/Profile/Environment` for first-pass profile-selected
  environment variable policy
- workspace `Profiles/Profile/Stage` for first-pass profile-selected config and
  content staging policy
- workspace `Profiles/Profile/Uses` for first-pass profile-selected
  project/package/tool/runtime dependency policy
- workspace `Profiles/Profile/Runtime` for first-pass profile-selected runtime
  module policy
- workspace `Profiles/Profile/<ProductKind>/Build` and
  `Profiles/Profile/<ProductKind>/Quality/Analyzer` for first-pass
  product-kind-specific profile policy
- workspace `Profiles/Profile/<ProductKind>/Environment` for first-pass
  product-kind-specific environment variable policy
- workspace `Profiles/Profile/<ProductKind>/Stage` for first-pass
  product-kind-specific config and content staging policy
- workspace `Profiles/Profile/<ProductKind>/Uses` for first-pass
  product-kind-specific dependency policy
- workspace `Profiles/Profile/<ProductKind>/Runtime` for first-pass
  product-kind-specific runtime module policy

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

Workspace remote package URLs and definition-driven project resolution beyond
first-pass profile policy are still future work.

### V4 Package Manifest Parsing

`Package SchemaVersion="4"` is accepted.

Implemented package parsing:

- package `Name`
- package `Version`
- package-level `Uses`
- package-level CMake integration metadata for local source providers
- `Library/Exports/Headers`
- `Library/Exports/Binary`
- `Library/Exports/LibraryTarget`
- `Tool/Exports/Tool`
- `Features/Feature`
- feature `Uses` dependencies
- feature `Build/Define`
- feature `Provides/Capability`
- feature dependency `Scope` metadata

The active repository package wrappers have been rewritten to the V4 package
contract shape. Legacy `Artifacts`, `Dependencies/PackageRef`, `Tools`,
`Modules`, and `Plugins` package sections are no longer used by the CLI package
catalog.

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

### Inspect And Composition Graph JSON

`ngin inspect --format json` now emits the same top-level V4 Composition Graph
envelope as `ngin graph --format json`; the older mixed inspect JSON wrapper has
been retired.

The graph JSON currently includes:

- schema version and graph kind
- graph state
- facet names
- identity and product metadata
- selected profile, host platform, target platform, ABI, toolchain, and
  environment
- named convention/default contributions
- first-pass property provenance
- summary counts
- package closure and provider roots
- selected package features
- build inputs and defines
- generator declarations
- stage plan
- runtime modules/plugins
- environment entries with secret redaction
- launch entries
- package outputs
- publish entries
- quality analyzer plans
- diagnostics

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
- `ngin add package` aliases the package-add workflow for the V4 product layer

`ngin add project-reference` can edit V4 product-first projects by inserting a
`Uses/Project` dependency with a logical name and path.

### Format Command

`ngin format` is available as the first V4 manifest formatting command.

Current behavior:

- formats the selected XML manifest with deterministic indentation
- supports `.nginproj`, `.nginpkg`, `.ngin`, and `.ngin.xml` parser-supported
  XML shapes
- validates through the XML parser before writing
- refuses manifests containing XML comments instead of silently dropping them,
  because the current XML parser tree does not preserve comments

### Schema Metadata Command

`ngin schema --format json` is available as the first editor-facing V4 schema
metadata command.

Current behavior:

- emits supported V4 file types
- emits product kinds
- emits dependency kinds and scopes
- emits common and product-specific sections
- emits build, stage, runtime, and environment item names
- emits object-identity explain kinds and graph plan names

### Restore Command

`ngin restore` is available as the first concrete package acquisition command.

Current behavior:

- resolves the selected project/profile package closure
- materializes package manifests or ZIP-backed `.nginpack` archives into a
  local package store
- extracts full `.nginpack` payloads into the package store
- resolves HTTP `.nginfeed` package source URLs and downloads referenced
  package artifacts into the package cache
- default store path is `.ngin/packages` under the workspace or project root
- `--output` can select a package store directory
- writes the deterministic `ngin.lock`
- `--locked` verifies the existing lock file before restoring and refuses graph
  drift
- preserves resolved dependency scopes in the lock file

Hashes, signed feed metadata, DEFLATE-compressed `.nginpack` entries, and
source/binary provider switching are still future work.

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

`file://` and HTTP package source URLs participate in package catalog
resolution as static feed roots.

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
- quality analyzer plans

`ngin diff --from-lock <old> --to-lock <new>` can compare deterministic lock
files and report package additions, removals, version changes, scope changes,
and source/provider changes.

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
- `convention:<Name>`
- `source:<path>`
- `define:<Name>`
- `stage:<relative-path>`
- `package:<Name>`
- `feature:<Package>/<Feature>`
- `generator:<Name>`
- `launch:<Name>`
- `publish:<Name>`
- `package-output:<Name>`
- `env:<Name>`
- `analyzer:<Name>`
- `runtime-module:<Name>`

This coexists with the older focused explain subcommands while V4 moves toward
object identity syntax.

### Focused Graph Plans

`ngin graph` accepts V4 north-star plan switches:

```bash
ngin graph --format json
ngin graph --build-plan
ngin graph --stage-plan
ngin graph --package-plan
ngin graph --package-output-plan
ngin graph --launch-plan
ngin graph --runtime-plan
ngin graph --environment-plan
ngin graph --publish-plan
ngin graph --quality-plan
```

Current behavior is text output over the existing resolved model. This is not
the final graph API, but it exposes focused plan views for build inputs,
staged files, package closure, launch metadata, runtime selection, environment
variables with secret redaction, produced package outputs, publish
declarations, and quality analyzer declarations.

`ngin graph --format json` now emits a graph-native top-level
`NGIN.CompositionGraph` JSON envelope with `schemaVersion: "4.0"`, identity,
selection, named convention/default contributions, first-pass property
provenance, facet summary, and first-pass plan payloads for package, build,
generate, stage, runtime, environment, package-output, launch, publish,
quality, and diagnostics.
`inspect --format json` emits this graph envelope directly.

Internally this output now starts from a first-pass `CompositionGraph` snapshot
instead of only streaming fields directly from resolver state. The snapshot
currently owns identity, product, selection, conventions, high-value
properties, summary counts, consumed packages, package features, build
defines, build inputs, active generators, staged files, environment entries,
package outputs, runtime modules/plugins, launch entries, publish entries,
quality analyzers, and provenance records for those nodes. Focused graph plan
JSON and matching text output for package, build, stage, runtime, environment,
package-output, launch, publish, and quality now consume those graph-owned
nodes.

`ngin graph --format json --<plan>-plan` emits a focused
`NGIN.CompositionGraphPlan` JSON envelope for the selected plan instead of the
full graph payload.

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
- `Kind="Archive"` with `Format="zip"`
- `Output`
- first-pass `Include` metadata for stage, runtime dependencies, and symbols
- `ngin publish [PublishName]`
- inspect JSON `publishes`
- graph `--publish-plan`
- profile diff publish entries

The current publish command builds and stages the selected product. Folder
publish copies the staged layout to the output directory and removes stale
output before copying the new staged layout. Archive publish writes a
deterministic uncompressed ZIP archive from the staged layout. Signing, SBOM,
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

The root `NGIN.ngin` workspace has been migrated to `Workspace
SchemaVersion="4"` with V4 `Projects`, `Defaults`, `Packages`, package
providers, and `Profiles`.

The old shared `Examples/Common.nginmodel` file has been removed. Workspace
shared declarations now belong in V4 workspace imports/definition fragments.

The active package wrappers under `Packages/` now use V4 package contract
shape. The CLI project-reference fixture manifests and the NGIN.Core BasicHost
sample manifest have also been migrated to product-first V4 shape.

Verified:

```bash
.ai/skills/test-cpp/scripts/test.sh hello-native
.ai/skills/test-cpp/scripts/test.sh hello-hosted
.ai/skills/test-cpp/scripts/test.sh hello-reflection
.ai/skills/test-cpp/scripts/test.sh ngin-core
.ai/skills/test-cpp/scripts/test.sh workspace
```

### NGIN.Core V4 Runtime Reader

`NGIN.Core` now treats V4 as its runtime-side manifest contract too. The
runtime builder no longer carries the V3 project parser branch, `.nginmodel`
resolution, project/profile template application, or legacy package root
sections.

Supported runtime-side V4 project/package surface:

- product-first project roots with exactly one product element
- `Application`, `Library`, `Tool`, `Test`, `Benchmark`, `Plugin`, `Module`,
  and `External` product identity normalization
- product `Uses` for projects, packages, tools, runtime dependencies, and
  nested dependency features
- product `Build`, `Stage`, `Runtime`, `Environment`, and `Launch`
- project-level `Profile` overlays with product fragments
- V4 package manifests with `Uses`, `Runtime/Bootstrap`, `Runtime/Module`,
  `Library/Exports`, `Tool/Exports`, and `Features`

This remains a runtime-side reader, not a duplicate full CLI graph resolver.
The CLI still owns complete V4 graph construction, package restore, build
planning, staging, and diagnostics.

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
Profile overlays can replace or remove package outputs by `Name`, and graph,
explain, and package packing use the selected effective package-output set.

`ngin package pack` can emit a standalone V4 `.nginpkg` manifest and a
deterministic ZIP-backed `.nginpack` artifact from a source project
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

The current `.nginpack` format is a ZIP-backed package archive. It contains
`package.nginpkg` plus package payload entries. Package cataloging extracts
archives into the package cache so graph resolution can see exported package
payloads before restore. `ngin restore` copies the archive into the package
store and extracts the full payload there.

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
- V4 inspect `compositionGraph.plans` graph-owned plan slices
- V4 first-pass named convention/default visibility in inspect, graph, and
  explain
- V4 `ngin new`
- V4 `ngin package add`
- V4 `ngin add package`
- V4 `ngin add project-reference`
- V4 `ngin package update`
- V4 `ngin package remove`
- V4 `ngin package pack`
- V4 ZIP-backed `.nginpack` package output
- V4 `ngin format`
- V4 `ngin schema --format json`
- V4 `ngin restore`
- V4 restore from `.nginpack` package archives
- V4 restore extracts full `.nginpack` package payloads into the package store
- V4 locked restore enforcement
- V4 package catalog resolution from `file://` package source URLs
- V4 static `.nginfeed` package index resolution from `file://` package
  sources
- V4 network `.nginfeed` package index resolution from HTTP package sources
- V4 network package archive download into the package cache
- V4 package archive extraction during cataloging so exported payload files
  participate in graph resolution
- V4 `ngin package sources list`
- V4 `ngin package sources add`
- V4 `ngin package sources remove`
- V4 `ngin test`
- V4 `ngin benchmark`
- V4 `ngin analyze`
- V4 `ngin stage`
- V4 `ngin publish` for folder publishes
- V4 `ngin publish` for ZIP archive publishes
- V4 folder publish stale output cleanup
- V4 test product dependencies
- V4 hosted application runtime/package/stage/environment/module parsing
- V4 profile `Uses` package feature overlays
- V4 profile `Uses` project reference removals
- V4 profile `Uses` package dependency removals
- V4 workspace project/source/version parsing
- V4 workspace default profile command selection
- V4 workspace profile policy on projects without local profiles
- V4 workspace language/backend build defaults
- V4 workspace profile build and analyzer policy contributions
- V4 product-kind-specific workspace profile build and analyzer contributions
- V4 workspace profile environment policy contributions
- V4 product-kind-specific workspace profile environment contributions
- V4 workspace profile stage policy contributions
- V4 product-kind-specific workspace profile stage contributions
- V4 workspace profile dependency policy contributions
- V4 product-kind-specific workspace profile dependency contributions
- V4 workspace profile runtime module policy contributions
- V4 product-kind-specific workspace profile runtime module contributions
- V4 platform definition ABI tag resolution into inspect selection
- V4 selected toolchain definition resolution into inspect selection
- V4 package export and feature parsing
- V4 source package provider override metadata
- V4 profile-to-profile diff
- V4 lock-file diff
- V4 object-identity explain
- V4 object-identity source and publish explanation
- V4 object-identity environment variable explanation
- V4 object-identity analyzer explanation
- V4 focused graph plan switches
- V4 `ngin graph --format json`
- V4 graph-native JSON envelope
- V4 graph-native focused plan JSON envelopes
- V4 graph-owned package/build/generator plan payloads in graph and inspect
- V4-only project/workspace/package schema validation
- V4 root workspace parsing
- V4 package wrapper parsing for the active repository packages
- V4 project-reference build and collision fixtures
- V4 quality graph plan switch
- V4 publish graph/inspect/diff surface
- V4 named launch metadata
- V4 bracket package version range resolution
- V4 stage identity collision diagnostics
- V4 profile generator identity removal and replacement
- V4 profile publish identity removal and replacement
- V4 profile package output identity removal and replacement
- V4 same-scope duplicate diagnostics for generator, publish, package output,
  runtime module, and environment variable identities
- V4 inspect package closure classification from dependency scopes
- resolved package scope metadata
- V4-only `NGIN.Core` runtime project/package manifest reader
- V4 `NGIN.Core` runtime manifest tests
- V4-only VS Code workspace/project/package parser surface
- VS Code `.nginmodel` language registration removed
- VS Code V3 schema artifact replaced with first-pass V4 project schema
- VS Code snippets rewritten to product-first V4 authoring
- VS Code project editor authoring now writes product-first V4 sections for
  profiles, uses, build inputs, staged config, environment variables, secrets,
  launch, and package feature overlays
- VS Code unit tests converted to V4 project/workspace/package/editor syntax
- practical `.nginproj` authoring guide rewritten for product-first V4
- docs project-model sample manifests rewritten to V4 project/package syntax
- implementation cleanup removed `V4`/`v4` naming from active CLI, runtime, VS
  Code parser/editor, and schema artifacts now that the legacy manifest paths
  have been removed
- VS Code project editor fallback parsing for old normalized `<Inputs>` and
  top-level feature override shapes removed
- `PackageBootstrapMode::BuilderHookV1` renamed to `BuilderHook`
- dependency overlays now treat dependency name as the identity and scope as
  mergeable metadata, including explicit `AddScope` and `RemoveScope`
  mutations for project and workspace `Uses` overlays
- package feature dependency requests are merged back through the dependency
  identity path so feature selection cannot create a duplicate package identity
- CLI tests are split into focused authoring, workspace, command authoring,
  package, product, overlay, graph, and facade files with shared test support

## Not Implemented Yet

The following are still open and should not be described as complete:

- final V4 Composition Graph data model
- final frozen graph JSON contract
- stable named convention graph contributions and final provenance records
  beyond the first-pass graph snapshot
- final V4 overlay duplicate diagnostics and provenance for every item family
  beyond the currently covered named product items
- full host/target dependency closure separation during restore/build
- full workspace profile stage/runtime/uses overlays and definition-driven
  project resolution
- full platform/toolchain compatibility semantics
- full ABI tag compatibility matching for binary package selection
- full external dependency adapters for system, CMake package, pkg-config,
  vcpkg, and Conan providers
- signing, SBOM, trust policy
- DEFLATE compression support inside ZIP-backed `.nginpack` archives
- full analyzer execution, formatter execution, coverage collection, and
  quality policy enforcement
- final graph diff engine over a stable graph JSON schema
- full V4 editor schema/completion metadata generated from the final schema
- VS Code integration test pass against a freshly built V4 CLI in the current
  workspace

## Next Iteration

The next implementation slice should focus on one of these paths:

- rename internal `PackageReference`/policy terminology where it leaks into V4
  diagnostics or generated metadata
- migrate remaining guides/examples/spec drafts outside the active V4 plans to
  V4 syntax
- rename internal `PackageReference`/policy terminology in the VS Code extension
  where it leaks into user-facing labels or generated metadata
- expand workspace profile product overlays beyond defaults
- continue hardening overlay identity/remove/override semantics for remaining
  non-package item families, especially launch edge cases, analyzer duplicates,
  and build-setting duplicates
- add DEFLATE support or a compression backend for ZIP-backed `.nginpack`
  entries if package size becomes important
