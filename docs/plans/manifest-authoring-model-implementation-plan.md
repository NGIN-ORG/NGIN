# NGIN Manifest Authoring Model Implementation Plan

Status: In progress
Created: 2026-08-10
Revised: 2026-08-10
Scope: project, package, and workspace manifests; CLI authoring and resolution;
Composition Graph and derived execution plans; CMake integration;
PackageProvider boundaries; editor support; canonical examples; documentation

## Implementation progress

| Milestone | Status | Milestone commit |
| --- | --- | --- |
| 0 — Ratify the complete contract | Complete | `docs(manifest): ratify authoring model contract` |
| 1 — ManifestSpec, authored AST, diagnostics | Complete | `refactor(manifest): add structural authoring foundation` |
| 2 — Selection, merge laws, paths, placeholders | Complete | `feat(manifest): add semantic selection foundations` |
| 3 — Direct project grammar and build items | Complete | `feat(manifest): implement direct project semantics` |
| 4 — Package instances, exports, Options, capabilities | Complete | `feat(manifest): implement package activation semantics` |
| 5 — Actions, Tools, execution trust | Complete | `feat(manifest): implement trusted Action semantics` |
| 6 — Complete resolver and immutable graph | Complete | `feat(manifest): resolve immutable composition graph` |
| 7 — Derived plans and CMake integration | Complete | `feat(manifest): derive plans through CMake adapter` |
| 8 — PackageProvider hardening and reproducibility | Complete | `feat(manifest): lock exact package artifacts` |
| 9 — Stage, launch, test, publish plans | Complete | `feat(manifest): derive deployment and publish plans` |
| 10 — Workspace, authoring commands, editor | Pending | — |
| 11 — Repository migration and old model deletion | Pending | — |
| 12 — Verification and release hardening | Pending | — |

## Purpose

This plan replaces the current manifest authoring model with one coherent XML
contract before NGIN's first official manifest release. It is a deliberate
pre-release redesign, not another numbered schema revision.

The model must preserve the central NGIN idea:

- authors describe products and intent;
- packages describe requirements and named exports;
- workspaces provide discovery and policy;
- NGIN resolves products, active package exports, and deployable artifacts into
  an immutable Composition Graph;
- deterministic execution plans drive build, actions, staging, launch, test,
  and publishing.

The smallest useful project must remain easy to read. Advanced behavior must
be available through typed concepts rather than encoded lists, generic
features, arbitrary conditions, or backend-specific strings in the semantic
model.

The implementation should land as reviewable changes, but the completed
repository must expose only the new grammar. No compatibility parser, format
detection, or silent fallback for the superseded grammar should remain.

## Delivery boundary

This overhaul designs explicit interoperability boundaries now, but it does
not implement additional build backends.

### In scope

- A backend-neutral authored semantic model and Composition Graph.
- A backend-neutral PackageProvider result and resolved-artifact boundary.
- CMake as the only implemented project build backend.
- CMake as the only implemented source-build and installed-package integration
  family.
- Migration of existing local/source package behavior and existing Conan/vcpkg
  PackageProvider behavior where it supplies packages to the CMake path.
- CMake `AddSubdirectory`, isolated configure/build/install, manual wrapper,
  and `FindPackage` behavior expressed as CMake-specific integration metadata.
- Backend capability validation so unsupported behavior fails explicitly.
- Package coordinates/instances, PackageProvider identities, integrity,
  host/target context, and provenance in resolution and dependency locking.
- Publish abstractions that are independent of the current CPack
  implementation.

### Not in scope for this overhaul

- Implementing Meson, Bazel, Autotools, Make, xmake, or MSBuild adapters.
- Generating Meson or Bazel projects.
- Adding new external PackageProviders or registries.
- Implementing CPS or `pkg-config` consumption or publication.
- Automatically generating production-ready Conan recipes or vcpkg ports.
- Adding new native installer families beyond the currently supported publish
  behavior.

The semantic contract and extension boundary must make those additions
possible later without changing ordinary project manifests, package export
identity, component activation, or graph semantics.

## Goals

- Keep XML as the sole authored manifest format.
- Describe one primary product directly on `<Project Type="...">` without a
  redundant product wrapper.
- Remove the ambiguous generic Module product Type in favor of Library, Plugin,
  or C++ module build items.
- Make a conventional application or library understandable without extensive
  documentation.
- Replace generic `<Feature>` switches with named exports, typed options,
  actions, capabilities, and automatic contributions.
- Separate package selection from export/component activation.
- Distinguish a logical PackageCoordinate from each host/target/build-specific
  PackageInstance.
- Let testing, tooling, publishing, and deployment context imply dependency role
  instead of using compound scope strings or a large family of dependency
  kinds.
- Avoid XML entity syntax in ordinary authoring, especially `&lt;` in version
  constraints.
- Use additive build conventions with explicit include, exclude, remove, and
  update behavior.
- Limit fundamental build selection to Configuration, Target, Toolchain, and
  typed Options.
- Treat named presets as convenient input expansion rather than semantic
  resolution dimensions.
- Use a semantic merge law appropriate to each category instead of forcing all
  values through one overlay precedence model.
- Unify exported generators and development tools under one package-side
  Action contract while keeping friendly project verbs.
- Keep application runtime composition in C++ and optional runtime frameworks,
  never in project/package manifests or the CLI resolver.
- Represent dynamically loaded plugins and other runtime needs as deployable
  artifacts, not declarative application modules.
- Keep CMake vocabulary in a schema-defined CMake integration extension.
- Keep CMake IntegrationBindings completely outside the semantic Composition
  Graph.
- Preserve source locations and provenance through parsing and resolution.
- Generate structural XSD, editor metadata, validation metadata, and reference
  material from one internal manifest specification.
- Make every command consume the Composition Graph or a deterministic plan
  derived from it; no command rereads authored XML.
- Support full semantic-version behavior, versioned capabilities, central
  version policy, local packages, PackageProviders, reproducible dependency
  locking, and canonical composition fingerprints.
- Provide actionable errors for invalid structure, invalid semantics,
  conflicts, cycles, ambiguity, unsafe paths, trust violations, and ineffective
  declarations.

## Non-goals

- Introducing JSON, YAML, TOML, or a scripting language as an alternative
  authoring model.
- Assigning a public manifest format number before the first official release.
- Preserving old root-level sections, profile overlays, `Scope` strings, or
  generic feature behavior through compatibility aliases.
- Defining one universal merge or override mechanism.
- Defining arbitrary user-created selection dimensions.
- Treating development, test, or production as an implicit compilation axis.
- Adding a general conditional expression language.
- Allowing arbitrary shell fragments in normal project or package manifests.
- Allowing package presence alone to execute exported tools on a developer
  machine.
- Making the Composition Graph a command-specific execution god-object.
- Coupling CLI/tooling execution to the optional `NGIN.Core` runtime framework.
- Describing dependency injection, runtime services, application modules,
  startup/shutdown ordering, or application behavior in manifests.
- Modifying third-party source trees or redesigning unrelated NGIN APIs.
- Building every possible selection combination by default.

## Target mental model

```text
Project
  identity
  target dependencies
  build items
  selected actions
  staging and deployable artifacts
  launch/testing/publishing intent
  refinements

Package
  identity
  requirements
  public options
  named exports/components
  libraries, Tools, Actions, Plugins, Assets
  automatic contributions
  backend integration extensions

PackageInstance
  PackageCoordinate
  PackageProviderResult
  host or target context
  derived binary compatibility
  artifact-affecting options

Workspace
  discovery
  package sources/PackageProviders
  central versions
  targets/toolchains
  defaults and policy
  presets

Resolver
  input selection
  constraint intersection
  component activation
  option assignment
  capability binding
  provenance

Composition Graph
  products
  active package exports
  semantic edges
  actions
  deployable artifacts
  contributions
  origins

Integration bindings
  semantic export to backend mapping
  CMake binding metadata outside the graph

Reproducibility identities
  dependency lock for acquired PackageInstances
  composition fingerprint for the canonical graph

Derived plans
  RestorePlan
  BuildPlan
  ActionPlan
  StagePlan
  LaunchPlan
  TestPlan
  PublishPlan
```

There is no generic Feature, universal Scope string, arbitrary Condition
language, semantic Profile object, generic Variant dimension, or single merge
precedence rule.

## Build composition versus application composition

The manifest system and an application runtime framework own different graphs:

```text
NGIN manifests and CLI
  What must be acquired, built, linked, generated, staged, and launched?

C++ application and optional runtime framework
  What services, modules, systems, and backends are registered?
  In what order do they initialize and shut down?
  How does the application behave?
```

The CLI-owned Composition Graph is a product/build/package/deployment graph. It
is not the application's live service or module graph.

`NGIN.Core` is an optional C++ library/framework. A project that uses its API
declares the package as a normal direct dependency. Application code creates
the host, registers services and modules, selects runtime backends, and owns
lifecycle ordering through NGIN.Core APIs.

Manifest capabilities apply only to acquisition, compilation, linking,
generation, artifact selection, or deployment. Runtime service requirements
and runtime module capabilities belong to application code and NGIN.Core.

True dynamic plugins remain visible to the manifest system only as products and
deployable artifacts. The CLI builds and stages a plugin; application code or a
runtime framework discovers, validates, loads, orders, and unloads it.

## Decisions this plan treats as settled

These decisions may change only through an explicit architecture decision,
not incidentally during parser or resolver implementation.

1. The authored root is `<Project>`, `<Package>`, or `<Workspace>` according to
   document kind.
2. Authored manifests have no public `SchemaVersion` before the official
   release. Project/package release versions remain normal domain data.
3. A project declares one primary product with `Type="Application"`,
    `Type="Library"`, `Type="Tool"`, `Type="Test"`, `Type="Benchmark"`,
    `Type="Plugin"`, or `Type="External"`. There is no generic Module product:
    linked reusable code is a Library, dynamically loaded code is a
    Plugin, and C++ language modules are build items within a product.
4. Project behavior appears directly under `<Project>` in semantically named
   sections.
5. Unknown core elements and attributes are errors. Backend integrations use
   registered XML namespaces with their own schemas and validators.
6. Document order is for readability and does not change semantics except in
   a collection explicitly defined as ordered. Prefer explicit `Before` and
   `After` relationships over order-dependent XML.
7. Lists use repeated elements, not semicolon-delimited strings.
8. Semantic categories have distinct merge laws. Version constraints
   intersect; dependencies and required contributions accumulate; scalar
   defaults refine; policy gates; capabilities resolve.
9. A package being resolved does not automatically activate all its exports.
   Consumers activate declared exports or the package's unambiguous default
   export set.
10. Package-level obligations apply when any export is active. Export-level
    runtime files and requirements apply only when that export is active.
11. Required runtime files and notices cannot be silently removed by a
    consumer. Optional content is a named selectable asset export.
12. Package configuration uses typed Options. An optional dependency without a
    typed activation mechanism is invalid.
13. Package-side generators, analyzers, formatters, validators, and custom
    tools share one Action contract. Project authoring retains typed verbs such
    as `<Generate>` and `<Analyze>`.
14. Selecting an Action creates its host-Tool edge. Selecting a Plugin or other
    deployable export creates its stage/deployment edges. Testing and publishing
    dependencies are declared inside their respective sections.
15. Fundamental build selection is Configuration + Target + Toolchain +
    Options. Target contains OS, Architecture, and platform environment/version
    facts. Toolchain contains compiler, language ABI, standard/runtime library,
    and linker facts. BinaryCompatibility is derived from both plus relevant
    Options. Named platform names are aliases for structured Targets, not an
    additional dimension.
16. Presets expand to concrete selection and command inputs before semantic
    resolution. A preset name does not affect graph identity.
17. Runtime/deployment environments belong to Launch, Stage, or Publish. A
    compile-time difference must be a named typed Option.
18. Application runtime composition is owned by C++ and optional runtime
    frameworks. Host, Module, runtime-service dependency, startup phase, and
    application lifecycle concepts do not exist in core or extension manifest
    grammar.
19. A Plugin export describes a dynamically loadable artifact and its required
    deployment files. Activating it never instructs a runtime framework to load,
    configure, or order it.
20. Semantic package metadata is backend-neutral. CMake build/discovery/target
    mapping lives in a CMake XML extension and resolves into immutable
    CMakeIntegrationBindings outside the Composition Graph.
21. CMake is the only executable backend delivered by this overhaul. The core
    must not claim support for an unimplemented backend.
22. The Composition Graph is immutable, purely semantic truth. Commands derive
    typed deterministic execution plans from it plus separately resolved
    integration bindings. Backend names and opaque backend data never enter the
    graph.
23. Structural XSD validation and complete semantic validation are distinct.
    Both derive from one internal ManifestSpec, but XSD is not required to
    encode cross-reference or resolver rules.
24. PackageCoordinate identifies a logical package name/version.
    PackageInstance identifies one acquired/built realization with its
    PackageProviderResult, context, derived BinaryCompatibility, and
    artifact-affecting Options. Exports activate on PackageInstances.
25. The dependency lock records exact acquired PackageInstances and only the
    selection facts that affect acquisition or artifacts. The canonical
    Composition Graph produces a separate composition fingerprint covering
    active exports, capability bindings, Actions, and other semantic choices.
26. Lock design begins with the contract, but serialization is finalized only
    after component activation, Options, capabilities, Actions,
    target/toolchain selection, and PackageProvider resolution are complete.
27. Every capability provision has a semantic Version. Requirements express a
    compatible/exact/structured Version constraint, and resolution creates a
    CapabilityBinding from the requirement to one active implementation export.
    CapabilityBindings may affect only build/package/deployment composition;
    they never represent runtime services or application modules.
28. Paths, globs, placeholders, symlinks, case behavior, staging safety, and
    canonical serialization are normative parts of the contract.
29. Exported actions never execute merely because their package is present.
    Execution requires an explicit project selection or deliberately enabled
    policy, and workspace trust policy may reject the tool's provenance.
30. Projects declare the libraries whose APIs their source uses directly, even
    when another dependency also requires those libraries transitively.
31. The completed CLI recognizes only the new grammar. Any development-only
    conversion utility must be removed before completion.

## Target authoring shape

The contract milestone must specify exact cardinality and validity. These
examples define the intended center of gravity.

### Conventional application

```xml
<Project Name="Hello.Native" Type="Application">
  <Dependencies>
    <Package Name="NGIN.Base" Compatible="0.4" />
  </Dependencies>

  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
</Project>
```

With standard source conventions, the `<Build>` section may be omitted. The
convention must have a documented name, predictable files, an explicit
disable/refinement mechanism, and `ngin explain` visibility.

### Package export activation

A package with one default library stays concise:

```xml
<Package Name="fmt" Exact="11.0.2" />
```

A package with multiple meaningful exports is explicit:

```xml
<Package Name="OpenSSL" Compatible="3">
  <Use Library="TLS" />
</Package>
```

The package may declare:

```xml
<Exports>
  <Library Name="Crypto" Default="true" />
  <Library Name="TLS">
    <Requires>
      <Export Package="OpenSSL" Library="Crypto" />
    </Requires>
    <RuntimeFiles>
      <File From="bin/ssl-runtime" Into="bin" />
    </RuntimeFiles>
  </Library>
</Exports>
```

Activating `TLS` activates its requirements and runtime contributions. Merely
resolving the package does not activate every export.

### Structured version constraints

```xml
<Package Name="NGIN.UI" Compatible="0.4" />
<Package Name="fmt" Exact="11.0.2" />

<Package Name="OpenSSL">
  <Version AtLeast="3.2.0" Before="4.0.0" />
  <Use Library="TLS" />
</Package>
```

The exact version contract must cover prerelease identifiers, build metadata,
exact versions, compatibility, minimum bounds, exclusive upper bounds, and
workspace-managed constraints without multiple spellings for one meaning.

### Actions with typed project verbs

Packages export one Action abstraction:

```xml
<Exports>
  <Tool Name="MetaGen" />

  <Action Name="ReflectionCodegen"
          Kind="Generate"
          Tool="MetaGen">
    <Inputs>
      <Header Include="include/**/*.hpp" />
    </Inputs>
    <Outputs>
      <Source Path="generated/reflection.cpp" />
    </Outputs>
  </Action>

  <Tool Name="clang-tidy" />

  <Action Name="Analyze"
          Kind="Analyze"
          Tool="clang-tidy" />
</Exports>
```

Projects use domain verbs:

```xml
<Generate Action="NGIN.Reflection.MetaGen::ReflectionCodegen" />

<Tooling>
  <Analyze Action="NGIN.Tooling.ClangTidy::Analyze" />
</Tooling>
```

The selection activates the exporting package and referenced Tool export on a
host PackageInstance. CMake integration maps that semantic Tool export to an
executable target. Package-private build helpers are not Tool exports, and
package availability alone never executes an Action.

```xml
<cmake:Target Export="MetaGen"
              Name="NGIN.Reflection.MetaGen" />
```

### Context-scoped dependencies and deployable exports

Ordinary dependencies are target/product dependencies:

```xml
<Dependencies>
  <Package Name="NGIN.UI" Compatible="0.4" />
</Dependencies>
```

Testing location creates a test-only dependency role:

```xml
<Testing>
  <Dependencies>
    <Package Name="Catch2" Compatible="3" />
  </Dependencies>
</Testing>
```

Runtime deployment is expressed by activating a typed export, not by declaring
application runtime composition:

```xml
<Dependencies>
  <Package Name="Example.Plugins" Compatible="2">
    <Use Plugin="Telemetry" />
  </Package>
</Dependencies>
```

The Plugin export contributes its binary and required RuntimeFiles to the stage
plan. It does not instruct the application to discover or load the plugin.
There is no generic `Optional="true"` or `Link="false"`; typed activation and
the selected export determine whether a dependency exists and what it
contributes.

### Application composition stays in C++

```xml
<Project Name="Gallery" Type="Application">
  <Dependencies>
    <Package Name="NGIN.Core" Compatible="0.4" />
    <Package Name="NGIN.UI" Compatible="0.4" />
    <Package Name="NGIN.UI.Hosting" Compatible="0.4" />
    <Package Name="NGIN.UI.Backend.SDL3" Compatible="0.4" />
  </Dependencies>
</Project>
```

The source code owns runtime behavior:

```cpp
auto builder = NGIN::Core::Application::CreateBuilder();
builder.UseUI();
builder.UseSDL3();
builder.AddModule<GalleryModule>();
return builder.Build().Run();
```

The manifest supplies code and deployable artifacts. It does not declare the
application host, service graph, modules, runtime requirements/provisions, or
startup/shutdown order. Libraries used directly by application source remain
direct dependencies even if another package also requires them transitively.

### CMake-specific package integration

Semantic exports remain independent of CMake target names:

```xml
<Package xmlns:cmake="urn:ngin:integration:cmake"
         Name="OpenSSL"
         Version="3.2.1">
  <Exports>
    <Library Name="Crypto" Default="true" />
    <Library Name="TLS" />
  </Exports>

  <Integrations>
    <cmake:FindPackage Name="OpenSSL">
      <cmake:Target Export="Crypto" Name="OpenSSL::Crypto" />
      <cmake:Target Export="TLS" Name="OpenSSL::SSL" />
    </cmake:FindPackage>
  </Integrations>
</Package>
```

Source-backed CMake integration uses the same namespace:

```xml
<Integrations>
  <cmake:AddSubdirectory Source=".">
    <cmake:Option Name="BUILD_TESTING" Value="OFF" />
    <cmake:Target Export="Library" Name="Example::Example" />
  </cmake:AddSubdirectory>
</Integrations>
```

The exact extension vocabulary is decided in the contract milestone. The
semantic core must not contain `FindPackage`, CMake cache variables, CMake
target names, generators, or toolchain-file concepts.

### Versioned capabilities

Capability implementations declare a semantic Version independently of their
package version:

```xml
<Library Name="TLS">
  <Provides>
    <Capability Name="NGIN.Net.TLS" Domain="Link" Version="1.0.0" />
  </Provides>
</Library>
```

Requirements use the same clear version forms as packages:

```xml
<Requires>
  <Capability Name="NGIN.Net.TLS" Domain="Link" Compatible="1" />
</Requires>
```

Capability identity is its Name plus Version. A CapabilityBinding records the
resolved requirement, compatible Version, and active implementation export.
The `PackageProvider` term is reserved for package acquisition; capability
resolution uses CapabilityImplementation and CapabilityBinding terminology.

## Semantic merge laws

There is no universal overlay precedence chain. Each category has one declared
merge law, identity rule, and conflict rule.

| Semantic category | Merge law |
| --- | --- |
| Scalar project setting | Convention default, then workspace default, then project assignment, then matched refinement, then allowed CLI assignment |
| Package version constraints | Intersection of every active direct, transitive, workspace, compatibility, and lock constraint |
| Required dependencies | Set union followed by graph closure; duplicate identity combines compatible constraints and activation requests |
| Package-level required contributions | Accumulate when any package export is active; consumer cannot remove them |
| Export-level required contributions | Accumulate when that export is active; consumer cannot remove them |
| Package option value | Package default, workspace assignment, project assignment, matched refinement; conflicting assignments at equal authority are errors |
| Project option value | Declared default, workspace default, project assignment, matched refinement, allowed CLI assignment |
| Workspace policy | Constraint/gate; project input must satisfy it and cannot override it |
| Build items | Include plus Exclude/Remove/Update over stable item identities |
| Capability requirements | Accumulate and intersect Version constraints; capability binding selects one compatible implementation export or reports ambiguity |
| Capability implementations | Accumulate on active exports; exclusive capabilities enforce one compatible binding per requirement context |
| Export activation | Set union and dependency closure; explicit exclusions are allowed only for non-required/default activation and cannot break requirements |
| Action selection | Set union by qualified Action identity; incompatible duplicate invocation identities are errors |
| Presets | Expand before semantic resolution; no preset merging occurs inside the graph |
| Lock input | Constrains resolution to recorded identities; never overrides incompatible authored constraints |

Milestone 0 must expand this table for every mergeable collection and scalar.
Implementation should use category-specific resolver operations rather than a
generic overlay framework with type-specific exceptions.

## Package resolution and export activation

Package identity, package acquisition, and export activation are distinct:

```text
package constraint
      |
      v
resolve PackageCoordinate and PackageProviderResult
      |
      v
construct host/target PackageInstance
      |
      v
activate requested/default exports on that instance
      |
      v
activate export requirements, actions, capabilities, and contributions
      |
      v
repeat until closure
```

The contract must define:

- default export rules, including the zero/default/multiple-default cases;
- qualified export identity and aliases;
- activation through direct dependencies, transitive requirements, Actions,
  Tools, Plugins, Assets, and CapabilityBindings;
- package-level versus export-level contributions;
- whether an export is public, private, development-only, or runtime-only by
  its typed kind and activation location;
- how public library requirements propagate to consumers;
- how inactive exports appear in `package show` but not in the active graph;
- cycles in export requirements and the diagnostic path;
- conflicts between mutually exclusive exports or incompatible instances in one
  final linkage closure;
- option-dependent exports through a small structured predicate model.

An option predicate may test a declared option value or structured Target fact.
It must not become arbitrary text evaluation, recursive expressions, or script
execution.

## Selection, refinements, and presets

### Semantic selection

The resolver receives exactly these fundamental inputs:

- `Configuration`, such as Debug or Release;
- `Target`, containing OS, Architecture, platform environment, and platform
  version constraints;
- `Toolchain`, containing compiler/linker, C++ ABI, standard library, and
  runtime-library facts;
- typed project and package `Options`.

A named platform is an alias for a Target declaration:

```xml
<Target Name="win-x64"
        OS="windows"
        Architecture="x64" />
```

The resolved graph stores the structured facts. Alias spelling does not change
semantic identity. NGIN derives BinaryCompatibility from the selected Target,
Toolchain, Configuration, and only the Options that affect produced artifacts.
The derived value, rather than an `ABI` attribute on Target, controls binary
PackageInstance compatibility.

Project-specific choices are typed options:

```xml
<Options>
  <Enum Name="Distribution" Default="Bundled">
    <Value Name="Bundled" />
    <Value Name="System" />
  </Enum>
</Options>
```

There is no arbitrary Variant dimension. There is no compile-time Environment
dimension. Development/production launch differences use named Launch or
Publish definitions. A genuine compilation difference is an explicit Option.

### Refinements

Refinements select on Configuration, structured Target facts, Toolchain facts,
and typed Option values. They refine only fields whose category merge law
permits refinement.

The contract must define matching specificity and reject two conflicting writes
at equal specificity. XML order is not a tie-breaker.

### Presets

Presets are workspace conveniences:

```xml
<Preset Name="dev">
  <Configuration Name="Debug" />
  <Target Name="host" />
  <Toolchain Name="default" />
  <Option Name="Distribution" Value="Bundled" />
  <Launch Name="Development" />
</Preset>
```

`ngin build --preset dev` expands this input before resolution. The graph is
identical to one produced by passing the same concrete selections directly.

## Backend and PackageProvider interoperability

The architecture must separate four contracts.

### PackageProvider contract

A PackageProvider locates or acquires a package and returns a normalized
PackageProviderResult:

- PackageCoordinate with NGIN name and exact resolved version;
- PackageProvider kind and native coordinate;
- source root, installed prefix, or archive location;
- source/binary integrity and revision identity;
- host or target context;
- declared or discovered artifact compatibility inputs;
- PackageProvider-owned lock identity;
- provenance and trust information.

Resolution combines that result with context, derived BinaryCompatibility, and
artifact-affecting Options to construct a PackageInstance. PackageProvider
results contain no semantic export activation and do not inject CMake settings
into the semantic graph.

The initial implementation supports the repository's current PackageProvider
needs.
The interface must allow future NGIN registries, system packages, Conan,
vcpkg, or other sources without changing project dependency grammar. Existing
Conan/vcpkg behavior may remain CMake-facing, but new PackageProvider features
are not part of this overhaul.

### Semantic package contract

The package manifest declares Options, requirements, Library/Tool/Action/Plugin
exports, capabilities, runtime artifact obligations, Assets, and compatibility.
These names and relationships are independent of the build system used to
produce them. They do not describe runtime services or application modules.

### Backend integration extension and bindings

A registered integration extension maps semantic exports/options to one
backend's source-build or discovery mechanism. Only the CMake extension is
implemented now.

Resolution produces an immutable backend-specific IntegrationBindings object,
separate from the Composition Graph. A CMake binding maps a semantic Product,
Library, or Tool export identity to its CMake project, target, discovery, or
source-build representation. BuildPlan derivation consumes both the pure graph
and selected CMakeIntegrationBindings; neither step rereads package XML.

The extension contract must expose capability metadata so validation can say
whether it supports:

- generated projects;
- source subdirectories;
- isolated configure/build/install;
- installed package discovery;
- imported targets;
- host tools;
- multi-configuration artifacts;
- cross compilation;
- generated sources and custom Actions;
- installation and runtime artifact discovery.

Future Meson, Bazel, or other integrations get their own namespace and adapter.
They must not force CMake terminology into core XML or graph nodes.

### Consumption and publication metadata

The resolved semantic artifact model must describe headers, libraries, Tools,
runtime files, configuration-specific artifacts, dependencies, and
BinaryCompatibility without relying on an opaque CMake target. CMake target
names and other opaque backend data exist only in CMakeIntegrationBindings and
the derived BuildPlan. A future CPS, `pkg-config`, Meson, or Bazel adapter can
populate portable artifacts or its own binding set without changing the graph.

Publishing similarly describes a semantic package or product layout first.
The current CPack implementation is an adapter used by the derived PublishPlan,
not part of the authored semantic vocabulary.

## Package identity and reproducibility

### PackageCoordinate and PackageInstance

The resolver uses two distinct internal identities:

```text
PackageCoordinate
  NGIN package name
  exact package version

PackageInstance
  PackageCoordinate
  PackageProviderResult identity
  host or target context
  derived BinaryCompatibility
  artifact-affecting Options
  artifact configuration/linkage when relevant
```

The same PackageCoordinate may legitimately produce separate host and target
instances, or multiple target instances when their acquired artifacts are
genuinely distinct. Export activation always names a PackageInstance.

Representability does not imply unrestricted duplication. Resolution must
reject incompatible instances of the same package in one final process/linkage
closure unless the package and platform explicitly support coexistence. The
diagnostic must show which activation path requested each instance.

BinaryCompatibility is derived, not authored as one opaque string. Its inputs
include relevant Target facts, Toolchain ABI/runtime facts, Configuration,
linkage, and only Options that affect the produced artifact. Header-only or
otherwise relaxed exports may declare which compatibility facts do not apply.

### Dependency lock

The package lock records exact acquired PackageInstances:

- PackageCoordinate;
- PackageProvider kind and native coordinate/revision;
- source or binary integrity;
- host or target context;
- artifact-affecting Options;
- BinaryCompatibility facts actually used to select/build the artifact;
- PackageProvider-native artifact/package identity;
- hermetic or non-hermetic status.

Selection values enter the dependency lock only when they change acquisition,
the dependency closure, or produced/selected artifacts. An active export is not
duplicated into the lock merely because it is active. If that export introduces
another package, the resulting PackageInstance is locked. If an Action requires
a Tool package, that Tool PackageInstance is locked.

### Composition fingerprint

The canonical Composition Graph produces a composition fingerprint covering:

- concrete Configuration, Target, Toolchain, and resolved Options;
- active exports;
- CapabilityBindings;
- selected Actions and Tool exports;
- semantic dependency edges, deployable Plugins, and contributions;
- exact PackageInstance references from the dependency lock.

The composition fingerprint is derived, not authored. It identifies the exact
semantic composition for caches, generated output invalidation, launch
descriptors, diagnostics, and optional CI verification. It is not a second
package lock and does not duplicate PackageProvider integrity records.

The user-facing guarantees must remain distinct:

- locked resolution guarantees the same dependency acquisition identities;
- matching composition fingerprints guarantee the same resolved semantic
  composition;
- plan fingerprints may additionally include executor/backend inputs without
  changing semantic graph identity.

## Paths, globs, placeholders, and trust

### Paths and globs

Milestone 0 must define:

- the base directory for every path-bearing field;
- when a workspace-relative path is permitted and how it is marked;
- accepted separators and canonical graph serialization;
- normalization of `.` and `..` and rejection of unsafe traversal;
- absolute path policy;
- symlink traversal, cycles, and workspace/package boundary behavior;
- case sensitivity independent of or intentionally inherited from the host;
- `*`, `?`, character-class, and `**` semantics;
- deterministic match ordering;
- default excludes for build and generated directories;
- staging destination safety and collision identity;
- whether canonical serialized graphs contain portable relative paths or
  machine-local paths in a separately marked field.

Graph identity and lock identity must not change merely because the workspace
was checked out at a different absolute directory.

### Placeholders

Replace general recursive textual substitution with a small set of typed,
phase-specific placeholders. Each placeholder declares:

- the sections where it is valid;
- its value type;
- when it resolves;
- whether it affects graph or lock identity;
- escaping and path-normalization behavior;
- the error for an unavailable value.

Unknown placeholders, recursive expansion, and using runtime values during
build resolution are errors.

### Action and PackageProvider trust

- Merely resolving a package never runs an exported Action.
- A project must explicitly select exported development Actions.
- Package-private build steps run only as part of an explicitly selected
  package acquisition/build path.
- Workspace policy can allow, deny, or require confirmation based on
  PackageProvider, signature, source, package, Action kind, or executable
  origin.
- CI can require locked, integrity-verified, non-interactive execution.
- Custom adapters use a typed executable protocol with declared inputs,
  outputs, environment, and capabilities; normal XML does not contain portable
  shell scripts.
- Explain and plan output show which executable will run and why before
  execution.

## Resolution and execution pipeline

```text
XML documents
      |
      v
Authored AST with source locations
      |
      v
Validated semantic model
      |
      +------> PackageProvider constraints ------> RestorePlan
      |                                                |
      |                                                v
      +<-------------------------------- PackageProviderResults
      |
      v
PackageInstances + selection + constraints + export activation
      |
      v
CapabilityBindings + Actions + contributions
      |
      v
Immutable Composition Graph
      |
      +------> composition fingerprint
      |
      +---- + CMakeIntegrationBindings ----> BuildPlan ----> CMake adapter
      +------> ActionPlan -----> tool executor
      +------> StagePlan ------> staging executor
      +------> LaunchPlan -----> launcher
      +------> TestPlan -------> test executor
      +------> PublishPlan ----> CPack/current publisher adapter
```

The Composition Graph contains semantic truth:

- selected products and concrete selection facts;
- exact PackageInstance and PackageProvider identities;
- active exports/components;
- typed semantic dependency edges;
- resolved options;
- versioned CapabilityBindings;
- Actions, Tool exports, and semantic inputs/outputs;
- package-level and export-level contributions;
- deployable Plugin and runtime-artifact relationships;
- artifact descriptions;
- provenance for every resolved fact.

It does not contain IntegrationBindings, backend target names, opaque backend
metadata, command lines, temporary directories, CMake invocation arguments,
copied-file work queues, or installer-tool commands. Derived plans contain
those deterministic execution details and retain graph-node references for
explanation.

No command may retain or inspect the authored project/package AST after graph
construction.

## ManifestSpec and validation strategy

One internal declarative ManifestSpec describes:

- core elements and attributes;
- parents, cardinality, identity, and basic types;
- enum values and documentation;
- extension namespaces and registration;
- structural product-section placement;
- semantic validator hooks;
- graph projection metadata where appropriate.

It generates or drives:

| Artifact | Responsibility |
| --- | --- |
| XSD | XML structure, namespaces, required fields, simple types, cardinality, basic placement |
| Editor metadata | Completion, hover text, examples, contextual placement, deprecations if ever introduced after release |
| CLI structural validator | Parser-equivalent structural checks with precise source locations |
| CLI semantic validator | Cross references, product rules, merge conflicts, cycles, constraints, activation, PackageProvider compatibility, trust, path safety |
| Reference documentation | Vocabulary, defaults, identity, merge law, graph projection, examples |

Parser and XSD must agree on structural acceptance. The CLI semantic validator
is intentionally stronger than XSD. The VS Code extension should invoke or
integrate CLI semantic validation rather than forcing all semantics into XSD.

## Milestone 0: Ratify the complete contract

### Objective

Freeze the semantic model and interoperability boundaries before parser work.

### Work

- Record architecture decisions for:
  - document roots and product identity;
  - omission of an authored manifest format number;
  - semantic merge laws;
  - PackageCoordinate, PackageInstance, PackageProviderResult, and export
    activation;
  - Configuration, Target, Toolchain, Options, refinements, and presets;
  - derived BinaryCompatibility;
  - Tool exports, Action unification, and execution trust;
  - the boundary between manifest composition and C++ runtime composition;
  - removal of Host/Module manifest concepts and the generic Module product
    kind;
  - Plugin exports and deployable runtime-artifact semantics;
  - PackageProvider, semantic package, IntegrationBindings, artifact, and
    publisher boundaries;
  - versioned capabilities and CapabilityBinding terminology;
  - CMake-only implementation scope;
  - Composition Graph versus derived execution plans;
  - structural versus semantic validation;
  - semantic versions, dependency-lock identity, composition fingerprints, and
    plan fingerprints;
  - paths, globs, placeholders, canonical serialization, and stage safety;
  - the flag-day migration policy.
- Produce a vocabulary table for every core element/attribute with parent,
  cardinality, identity, type, default, merge law, activation behavior, and
  graph projection.
- Produce a separate CMake extension vocabulary and capability Version rules.
- Define the valid section matrix for every product type.
- Define default exports and activation closure precisely.
- Define conventional file discovery and explicit refinement/disable behavior.
- Define the complete version constraint grammar.
- Define Option types, assignment authority, predicates, and conflict rules.
- Define portable BinaryCompatibility derivation from Target, Toolchain,
  Configuration, Options, runtime, and linkage facts.
- Define PackageProviderResult before the package resolver is implemented.
- Define capability Version declaration/constraint semantics.
- Define host/target separation for Tools and PackageProviders.
- Define strict direct-dependency behavior for libraries used by product source.
- Define which capability use cases belong to build/package/deployment
  composition and reject runtime service/module capabilities.
- Create representative proposed manifests and expected semantic inventories
  for minimal, library, framework-based application, dynamic Plugin, testing,
  generation, tooling, PackageProvider, multi-export, and multi-target cases.
- Map every existing manifest construct to a new construct or explicit removal.

### Deliverables

- Architecture decisions under `docs/architecture/decisions/`.
- Normative grammar and semantics references under `docs/reference/`.
- CMake integration extension reference.
- Golden authoring fixtures and expected semantic inventories.
- Existing-construct migration matrix.

### Completion criteria

- Every current behavior has a target concept or intentional removal.
- No example depends on generic Features, compound Scopes, encoded comparison
  operators, numbered manifest declarations, generic Variants, or a universal
  precedence rule.
- No example declares a runtime Host, application Module, service requirement,
  lifecycle phase, or runtime ordering in XML.
- The CMake mapping can represent current packages without placing CMake names
  in semantic exports or the Composition Graph.
- A future backend/PackageProvider can be described at the contract boundary without
  changing project dependency or export activation XML.

## Milestone 1: ManifestSpec, authored AST, and diagnostics

### Objective

Build the single structural specification and source-located authored model.

### Work

- Implement ManifestSpec metadata for project, package, workspace, and
  registered integration namespaces.
- Define typed authored AST nodes separate from semantic and graph nodes.
- Define core PackageCoordinate, PackageProviderResult, PackageInstance,
  BinaryCompatibility, CapabilityBinding, and backend IntegrationBindings
  boundary types without implementing resolution yet.
- Keep registered integration-extension AST/data types outside semantic graph
  types from the start.
- Preserve element/attribute source locations and original manifest identity.
- Generate strict structural XSD and editor metadata.
- Implement structural parser diagnostics with stable codes.
- Add semantic-validator hooks without implementing resolution in the parser.
- Test structural parser/XSD agreement and extension namespace behavior.

### Completion criteria

- Unknown core names and unregistered integration namespaces fail at source.
- XSD and parser agree on structure while semantic rules remain CLI-owned.
- Generated schema and editor artifacts are deterministic and drift-checked.

## Milestone 2: Selection, merge laws, paths, and placeholders

### Objective

Implement the foundational rules used by every later resolver stage.

### Work

- Implement Configuration, structured Target, Toolchain, and typed Options.
- Implement platform aliases as Target aliases.
- Implement preset expansion before semantic resolution.
- Implement structured refinement matching, specificity, and ambiguity errors.
- Replace general overlay operations with category-specific merge operations.
- Implement source-located constraint intersection and conflict reporting.
- Implement normative path normalization, globbing, canonical identity, and
  staging path validation.
- Implement the fixed typed placeholder registry and phase checks.
- Add deterministic serialization primitives that avoid checkout-root identity.

### Tests

- Extend `OverlayTests.cpp`, `WorkspaceTests.cpp`, `AuthoringTests.cpp`, and
  graph-focused tests.
- Cover order independence, equal-specificity conflict, preset equivalence,
  option typing, target aliases, derived BinaryCompatibility facts, glob
  ordering, case policy, symlinks, traversal rejection, and placeholder phase
  errors.

### Completion criteria

- No general profile/variant overlay object is needed by later milestones.
- Each implemented category names its merge law and conflict rule.
- Equivalent selections and paths produce identical semantic identities.

## Milestone 3: Direct project grammar and build items

### Objective

Implement the direct product-first Project model and conventional build inputs.

### Work

- Parse root identity and product Type.
- Parse direct project sections and enforce product validity semantically.
- Remove the generic Module product Type; use Library for linked reusable code,
  Plugin for dynamically loaded products, and typed build items for C++ module
  interface/implementation units.
- Implement conventional source/header/resource discovery.
- Implement Include, Exclude, Remove, and Update over stable item identities.
- Implement typed build settings without backend-specific names.
- Implement ordinary target dependencies plus testing/publishing-context
  dependency containers.
- Replace encoded lists with repeated elements.
- Reject old product wrappers and normalized root-level forms.

### Tests

- Extend `AuthoringTests.cpp`, `CommandAuthoringTests.cpp`, and
  `ProductTests.cpp`.
- Cover every product type, minimal documents, convention defaults, item
  operations, invalid placement, duplicates, and strict rejection of old forms.

### Completion criteria

- A minimal application and library need no redundant wrapper.
- Module is rejected as a product Type unless a future architecture decision
  defines a backend-neutral artifact distinct from Library and Plugin.
- The semantic project model contains no CMake field.
- Build-item results and origins are graph-ready and deterministic.

## Milestone 4: Package instances, exports, options, and capabilities

### Objective

Implement normalized PackageProvider results, PackageInstances, export
activation, and versioned capabilities without generic Features.

### Work

- Implement package and project target dependencies.
- Implement the PackageProvider interface and normalized PackageProviderResult
  used by the resolver, backed initially by the local/current PackageProvider
  paths needed by fixtures.
- Implement PackageCoordinate and construction of context-specific
  PackageInstances with derived BinaryCompatibility.
- Implement duplicate/coexistence rules for multiple instances of one
  coordinate in a final linkage closure.
- Implement package identity separately from named export identity.
- Implement default export rules and explicit `<Use>` activation.
- Implement export requirement closure and public/private propagation by typed
  export semantics.
- Implement package-level and export-level required contributions.
- Implement typed package Options, assignments, validation, and restricted
  predicates.
- Implement named optional Asset exports.
- Implement Plugin exports as dynamically loadable products/artifacts with
  automatic deployment contributions but no load/order/lifecycle semantics.
- Implement capability Version declarations and constraints,
  CapabilityImplementations, CapabilityBindings, and ambiguity/conflict
  behavior.
- Reject capabilities that attempt to model runtime services, application
  modules, dependency injection, or lifecycle ordering.
- Remove generic Optional and Link dependency traits.
- Implement full semantic-version constraint intersection.
- Keep acquisition behind PackageProviderResult and defer PackageProvider
  hardening plus dependency-lock serialization.

### Existing feature migration matrix

| Current use | Replacement | Activation behavior |
| --- | --- | --- |
| `NGIN.UI.Backend.SDL3:RuntimeNotices` | Package-level required Notice contribution | Applies whenever any SDL3 backend export is active |
| `NGIN.UI:RuntimeAssets` | Export-level required runtime files for required data; named Asset export for optional collections | Applies only to the export that needs it or when the Asset is explicitly selected |
| `NGIN.Core:Reflection` | Typed Boolean Option plus option-dependent requirements/exports/capabilities | Effects are explicit in option resolution and activation provenance |
| `NGIN.Reflection.MetaGen:ReflectionCodegen` | Action export with `Kind="Generate"` | Activated by project `<Generate>` selection |
| `NGIN.Tooling.ClangTidy:Analyzer` | Action export with `Kind="Analyze"` | Activated by project tooling selection, never package presence |
| Formatter features | Action export with `Kind="Format"` | Activated by project tooling selection |
| TLS/crypto provider features | Versioned Capability implementations on the relevant Library export | A CapabilityBinding activates the compatible implementation export |

### Tests

- Extend `PackageTests.cpp`, `WorkspaceTests.cpp`, and
  `GraphInspectTests.cpp`.
- Cover default/no/multiple exports, explicit activation, transitive export
  closure, version diamonds, option conflicts, required contributions, Assets,
  Plugin deployment without runtime activation, capability Version
  compatibility/binding, rejection of runtime-service capabilities, host/target
  instances, incompatible duplicate instances, and inactive-export absence.

### Completion criteria

- OpenSSL Crypto and TLS can be activated independently.
- Host and target uses of one PackageCoordinate resolve to distinct, correctly
  keyed PackageInstances.
- Runtime contributions follow the relevant activation unit.
- No repository use case requires a generic Feature.

## Milestone 5: Actions, Tools, and execution trust

### Objective

Implement one package-side execution abstraction without introducing application
runtime semantics.

### Work

- Implement Action export kinds Generate, Analyze, Format, Validate, and
  Custom over one internal contract.
- Implement Tool exports as named host-executable components, distinct from
  package-private build helpers and target/runtime executables.
- Implement typed inputs, outputs, Tool-export reference, arguments, working
  directory, environment requirements, determinism, and generated item
  contributions.
- Implement friendly project `<Generate>` and tooling verbs as Action
  activation surfaces.
- Implement explicit trust policy and pre-execution explanation.
- Resolve Action Tool exports on host PackageInstances.
- Reject Action metadata that attempts to configure application services,
  runtime modules, or lifecycle behavior.

### Tests

- Extend `ToolingTests.cpp`, `FacadeTests.cpp`, `PackageTests.cpp`, and
  graph-focused tests.
- Cover explicit action activation, no execution by package presence,
  Tool-export validation, host/target PackageInstance separation, trust denial,
  generated source contribution, output collisions, and rejection of runtime
  composition disguised as Action metadata.

### Completion criteria

- Reflection generation and clang tooling share one package-side Action model.
- Every exported Action references a valid Tool export.
- The project surface remains domain-specific and readable.
- Core and extension manifest/parser code contains no Host, runtime Module,
  service graph, or application lifecycle vocabulary.

## Milestone 6: Complete resolver and immutable Composition Graph

### Objective

Resolve the entire semantic model into one deterministic, provenance-rich graph
before locking or command execution is finalized.

### Work

- Implement the resolution pipeline for selection, constraints, package
  instances, export activation, Options, CapabilityBindings, Actions, Tools,
  Plugins, artifacts, and contributions over normalized PackageProviderResults.
- Make the graph immutable after successful resolution.
- Store semantic nodes and edges rather than backend commands.
- Exclude CMakeIntegrationBindings and all other backend data from graph types
  and serialization.
- Attach source and resolution provenance to every non-trivial fact.
- Remove command access to authored AST and old resolved/profile god-objects.
- Implement semantic `inspect`, `diff`, and `explain` over graph identities.
- Define deterministic graph serialization independent of XML order and
  checkout path.

### Tests

- Extend `Resolution`, `GraphInspect`, `Facade`, and focused command tests.
- Prefer exact semantic inventories and graph invariants over broad snapshots.
- Cover closure, determinism, provenance, cycles, CapabilityBindings,
  PackageInstance identity, and equivalent-preset identity.

### Completion criteria

- All semantic decisions are complete before backend plan derivation.
- No graph node requires the original manifest object for interpretation.
- No graph node contains an IntegrationBinding or backend vocabulary.
- Repeated equivalent resolution produces byte-stable canonical graph output.

## Milestone 7: Derived plans and CMake-only integration

### Objective

Turn the graph into deterministic plans and implement the CMake adapter without
polluting semantic contracts.

### Work

- Define common plan identity, graph references, provenance, validation, and
  deterministic serialization.
- Implement BuildPlan and ActionPlan derivation.
- Implement the registered CMake integration namespace and semantic validators.
- Resolve that extension into an immutable CMakeIntegrationBindings sidecar
  keyed by semantic graph Product/Export/Tool identities.
- Map generated NGIN products to CMake targets.
- Map package semantic exports to CMake imported/source targets.
- Support the CMake integration modes required by current packages, with
  explicit names rather than a fake backend-neutral Mode.
- Keep CMake cache variables and target names inside the adapter.
- Add CMake adapter capability checks and actionable unsupported errors.
- Keep opaque CMake target references exclusively in
  CMakeIntegrationBindings and BuildPlan, never in semantic artifact or graph
  metadata.
- Keep future adapter registration possible without loading or implementing
  another backend.

### Tests

- Extend `Build`, `Package`, `GraphInspect`, and command-focused tests.
- Cover generated targets, CMake source integration, installed discovery,
  manual wrappers, export-to-target mapping, host tools, cross-target inputs,
  and unsupported adapter capability diagnostics.
- Assert that semantic graph serialization contains no CMake cache variables,
  generated commands, target names, IntegrationBindings, or other opaque
  backend metadata.

### Completion criteria

- Every current supported build/package path works through the CMake adapter.
- BuildPlan derivation consumes the pure graph plus CMakeIntegrationBindings
  without rereading package XML.
- CMake remains excellent and explicit rather than masquerading as universal.
- No other backend is claimed or silently approximated.

## Milestone 8: PackageProvider hardening and reproducibility

### Objective

Harden PackageProvider implementations and finalize the dependency lock plus
composition-fingerprint guarantees after complete semantics are known.

### Work

- Complete local/source and currently supported external PackageProvider
  behavior needed by the CMake path using the normalized interface introduced
  in Milestone 4.
- Keep PackageProvider-native version/revision semantics rather than pretending every
  PackageProvider is identical.
- Finalize the dependency lock around acquired PackageInstances:
  - PackageCoordinate and PackageProvider coordinate;
  - source/binary revision and integrity;
  - host versus target context;
  - derived BinaryCompatibility facts used by the artifact;
  - artifact-affecting Options, configuration, and linkage;
  - PackageProvider-native artifact identity and hermeticity.
- Do not copy active exports, CapabilityBindings, or Action selections into the
  dependency lock unless they change acquisition/dependency closure; lock any
  PackageInstances they cause to be acquired.
- Implement canonical Composition Graph hashing as the composition fingerprint.
- Define plan fingerprints separately for backend/executor caching when needed.
- Implement lock reuse, verification, invalidation explanation, composition
  fingerprint reporting/verification, and locked CI behavior.
- Mark inherently non-hermetic PackageProviderResults honestly rather than
  claiming reproducibility.

### Tests

- Extend restore, package, workspace, and lock-focused tests.
- Cover host/target PackageInstances, PackageProvider-native identities,
  artifact-affecting versus composition-only Option changes, export activation
  that does/does not change dependency closure, Action Tool packages, integrity
  mismatch, non-hermetic results, exact lock reuse, composition fingerprint
  changes, and useful invalidation reasons.

### Completion criteria

- The dependency lock records every acquired artifact identity needed for
  reproducible dependency resolution without becoming a duplicate graph.
- Configuration/export/Action changes that do not affect acquisition leave the
  package lock stable but change the composition fingerprint.
- Artifact-affecting changes select or build a different PackageInstance and
  therefore update or invalidate the dependency lock.

## Milestone 9: Stage, launch, test, and publish plans

### Objective

Complete staging, launch, testing, and publishing through derived plans.

### Work

- Derive StagePlan from package/export contributions and project staging
  intent.
- Implement ownership-aware collision, replacement, missing-source, and safe
  destination behavior.
- Derive LaunchPlan with executable, arguments, working directory, environment,
  staged runtime-library paths, and process-level prerequisites.
- Derive TestPlan for product and testing-context dependencies.
- Derive PublishPlan for folder, archive, and current installer families.
- Keep publish intent backend-neutral while using the current CPack adapter.
- Model licenses, notices, symbols, and runtime artifacts as typed plan inputs.
- Ensure each executor consumes only its typed plan.

### Tests

- Extend staging, launch, product, publish, and framework-application focused
  tests.
- Cover automatic package/export files, ownership, collisions, repeated
  arguments, launch environments, Plugin deployment without implicit loading,
  and deterministic publish layouts.

### Completion criteria

- The hosted gallery manifest contains only direct dependencies, build inputs,
  stage intent, and launch intent; its services/modules/lifecycle exist only in
  C++ and NGIN.Core.
- Every staged/published file has an owner and reason.
- CPack commands do not appear in semantic manifest or graph contracts.

## Milestone 10: Workspace, authoring commands, and editor experience

### Objective

Make workspace policy and everyday authoring concise and discoverable.

### Work

- Implement explicit project discovery and normative glob behavior.
- Implement local packages, central versions, package sources/PackageProviders,
  Targets, Toolchains, defaults, policies, and Presets.
- Validate unused versions, overlapping discovery, duplicate PackageProviders,
  ambiguous Targets, and policy conflicts.
- Update `ngin new` templates for each product Type.
- Add/update commands for dependencies and export use, Options, Actions, Assets,
  Plugins, Targets, and Presets.
- Implement a canonical formatter that preserves comments where practical and
  emits repeated typed elements rather than encoded lists.
- Expand `validate`, `inspect`, `diff`, `explain`, and `package show` for merge
  laws, inactive/active exports, PackageProviderResults, Options, Actions,
  capabilities, contributions, and trust.
- Update VS Code to use generated structural schemas/editor metadata and CLI
  semantic validation.

### Completion criteria

- A beginner can create, validate, build, and launch a conventional project
  without learning package integration internals.
- A package author can inspect the CMake mapping separately from semantic
  exports.
- An advanced user can explain every activation, assignment, and plan step.

## Milestone 11: Repository migration and old model deletion

### Objective

Convert all authored inputs and documentation, then remove the old grammar and
resolver machinery in one controlled integration sequence.

### Migration policy

This is a pre-release flag-day migration. Development branches may temporarily
contain isolated converters or old fixtures. The completed branch must not ship
two grammars, automatic old-format detection, aliases, profile fallbacks, or
generic Feature behavior.

### Migration batches

1. **Golden semantic fixtures**
   - Establish expected Project, Package, activation, Action, Plugin, Graph,
     and plan inventories before converting production examples.
2. **Workspace and canonical projects**
   - Convert `NGIN.ngin`, `Hello.Native`, `Hello.Hosted`, and
     `Hello.Reflection`.
3. **Foundational package wrappers**
   - Separate semantic exports from CMake integration mappings.
   - Convert public libraries, Tools, runtime files, and PackageProvider metadata.
4. **Feature removal**
   - Apply the migration matrix to UI, reflection, tooling, TLS, crypto, and
     other affected packages.
5. **Advanced products**
   - Convert gallery products, tests, benchmarks, Plugins, former Module
     products, External products, and CLI target/option refinements.
   - Convert former Module products to Library or Plugin according to their
     produced artifact.
6. **Focused test fixtures**
   - Convert positive fixtures.
   - Keep old forms only in named rejection fixtures.
7. **Documentation and help**
   - Rewrite project, package, workspace, selection, action, hosting, staging,
     publishing, variables, CLI, interoperability, graph, and plan references.
   - Update root and Tools READMEs.
8. **Old model deletion**
   - Remove old parser branches, product wrappers, profile overlays, universal
     Scope parsing, generic Feature activation, duplicated generator/tool
     models, Host/Module/service/lifecycle manifest handling, hardcoded runtime
     phases, universal overlay machinery, old schema generation, command AST
     rereads, and conversion-only code.

### Migration ledger

Maintain a temporary checked-in ledger recording for each manifest:

- old constructs and target constructs;
- package and active export identities;
- expected constraints and semantic edges;
- expected Options and CapabilityBindings;
- expected Actions, Tool/Plugin exports, and deployment contributions;
- expected PackageCoordinates, PackageInstances, and PackageProviderResults;
- expected contributions and derived plan effects;
- backend integration mapping;
- intentional behavior changes.

Delete it only after the information becomes focused tests or durable docs.

### Completion criteria

- Every authored `.ngin`, `.nginproj`, and `.nginpkg` uses the new grammar.
- Active manifests contain no Feature, compound Scope, numbered manifest
  declaration, encoded version comparison, generic Variant, or old product
  wrapper.
- CMake terminology occurs only in registered integration XML, adapter code,
  generated CMake, and CMake-specific docs/tests.
- Old syntax exists only in focused rejection fixtures.
- No production compatibility or old overlay path remains.

## Milestone 12: Verification and release hardening

### Objective

Prove strictness, determinism, safety, explainability, and completeness.

### Verification layers

1. **Structural grammar**
   - Positive fixture for every structural form and registered namespace.
   - Negative fixture for unknown names, invalid placement, and old syntax.
2. **Semantic validation**
   - Product validity, cross references, merge conflicts, constraints, unsafe
     paths, trust, activation cycles, PackageProvider ambiguity, and unsupported
     backend capabilities.
3. **Resolution**
   - Exact semantic inventories for selection, exports, Options, capabilities,
     Actions, Tools, Plugins, contributions, and provenance.
4. **Derived plans**
   - Deterministic Build, Action, Stage, Launch, Test, and Publish plans with
     graph references.
5. **CMake integration**
   - Generated products, source packages, installed packages, manual wrappers,
     host Tools, and current external-PackageProvider inputs.
6. **Canonical examples**
   - Validate Hello.Native, Hello.Hosted, and Hello.Reflection.
   - Build or graph only examples affected by downstream changes during normal
     milestone verification.
7. **Final workspace integration**
   - Build `ngin_cli` and run `NGINCliTests`.
   - Run `ngin.workflow` and workspace `ctest` at the final integration boundary
     or when narrower checks reveal cross-cutting risk.

### Quality gates

- Equivalent inputs produce deterministic graphs and plans.
- Dependency-lock identity changes only when acquisition/dependency closure or
  artifact identity changes; semantic composition changes are captured by the
  composition fingerprint.
- No command rereads authored XML after graph construction.
- Every conflict reports contributing sources and the category merge law.
- Every implicit convention is visible through explain.
- Every package runtime contribution identifies package-level or export-level
  activation.
- Every executed tool has explicit activation and trusted provenance.
- Every Action resolves a Tool export on a host PackageInstance distinct from
  target dependencies.
- Every selected capability identifies its Version requirement,
  implementation export, and CapabilityBinding.
- Structural schema and parser remain aligned; semantic CLI validation covers
  rules beyond XSD.
- CMake is the only implemented backend and no semantic core type depends on
  CMake.
- CMake target names exist only in CMakeIntegrationBindings and derived plans.
- No manifest, semantic model, graph, or plan contains application Host, Module,
  service-registration, or lifecycle-ordering semantics.

## Repository ownership map

| Area | Primary locations | Responsibility |
| --- | --- | --- |
| ManifestSpec and authored AST | `Tools/NGIN.CLI/src/Model.hpp`, `Tools/NGIN.CLI/src/Authoring.cpp` | Core/extension structure, source locations, structural parsing |
| Merge and semantic resolution | `Tools/NGIN.CLI/src/Overlay.cpp`, `Tools/NGIN.CLI/src/Resolution.cpp` | Category merge laws, PackageInstances, selection, constraints, activation, CapabilityBindings, provenance |
| Graph and plan derivation | `Tools/NGIN.CLI/src/` graph/plan modules | Pure immutable semantic graph, composition fingerprint, and typed Restore/Build/Action/Stage/Launch/Test/Publish plans |
| CMake adapter | `Tools/NGIN.CLI/src/Build.cpp` and focused adapter modules | CMake extension validation, CMakeIntegrationBindings, target mapping, generation, invocation |
| Commands | `Tools/NGIN.CLI/src/Commands.cpp` | Authoring, validation, restore, build/deployment orchestration, inspect/diff/explain |
| Publishing adapter | `Tools/NGIN.CLI/src/Publishing.cpp` | Current CPack implementation behind PublishPlan |
| Focused tests | `Tools/NGIN.CLI/tests/` | Narrow behavioral, negative, graph, plan, and adapter coverage in existing files |
| Editor support | `Tools/NGIN.VSCode/` | Generated structural schemas, metadata, completion, CLI semantic diagnostics |
| Examples | `Examples/` | Minimal, framework-based, reflection, UI, testing, Plugin, and advanced patterns |
| Package wrappers | `Packages/` | Semantic exports/options plus explicit CMake integration metadata |
| Workspace | `NGIN.ngin` | Discovery, central constraints, PackageProviders, Targets, Toolchains, defaults, policy, Presets |
| Public contract | `docs/reference/`, `docs/guides/`, READMEs | Normative grammar, interoperability boundaries, and task guidance |
| Durable decisions | `docs/architecture/decisions/` | Rationale and architectural constraints |

Follow the nearest `AGENTS.md` before editing a subtree. Do not edit generated
build output, staged layouts, or `*.nginlaunch` files to implement the model.

## Suggested reviewable change-set sequence

1. Architecture decisions, vocabulary, merge-law table, and golden semantic
   inventories.
2. ManifestSpec, authored AST, source locations, structural XSD, and diagnostics.
3. Selection, typed Options, refinements, presets, paths, globs, and
   placeholders.
4. Direct Project grammar and additive build items.
5. PackageProviderResult, PackageCoordinate/PackageInstance, export activation,
   dependencies, contributions, Options, Assets, and versioned capabilities.
6. Tool exports, unified Actions, and execution trust.
7. Complete resolver and immutable Composition Graph.
8. CMakeIntegrationBindings, derived plan types, and the CMake adapter.
9. PackageProvider hardening, dependency lock, and composition fingerprint.
10. Stage, Launch, Test, and Publish plan execution.
11. Workspace authoring, CLI helpers, formatter, schema, and editor integration.
12. Canonical examples and foundational package migration.
13. Remaining manifests, focused fixtures, docs, and help migration.
14. Old grammar/model deletion and strict rejection coverage.
15. Integration verification and release hardening.

## Risks and mitigations

| Risk | Mitigation |
| --- | --- |
| A nicer grammar preserves the current resolver complexity | Replace universal overlays with explicit category merge laws and activation closure before parser migration |
| Multi-export packages activate too much | Separate package resolution from named/default export activation and attach contributions to the correct activation unit |
| Options recreate an untyped feature bag | Require declared types, defaults, descriptions, allowed values, limited predicates, and graph-visible assignments |
| Selection recreates profile explosion | Limit semantics to Configuration, Target, Toolchain, and Options; keep Presets outside graph identity |
| Runtime environments silently alter builds | Keep them in Launch/Stage/Publish definitions; require a typed Option for real compile-time differences |
| Action unification makes project XML generic | Unify the package/internal contract while retaining typed project verbs and validation by Action kind |
| Package presence executes untrusted tools | Require explicit Action selection and enforce workspace provenance/trust policy before plan execution |
| Manifest authoring grows into an application language | Prohibit Host, Module, service, DI, and lifecycle semantics; keep runtime composition in C++/NGIN.Core |
| Dynamic plugins reintroduce runtime composition indirectly | Model only the Plugin artifact and staging contribution; runtime discovery/loading/ordering remains framework-owned |
| Composition Graph becomes another god-object | Keep it immutable and semantic; derive command-specific deterministic plans |
| XSD becomes unmaintainable | Generate structural XSD and editor metadata from ManifestSpec; keep complete semantics in CLI validation |
| CMake terminology leaks into permanent semantics | Keep CMake extension data in CMakeIntegrationBindings outside the graph and assert pure graph serialization |
| Future backend support still requires a redesign | Separate PackageProviderResult, semantic exports, integration extensions, portable artifacts, graph, and plans now; test the absence of CMake from core types |
| Abstracting future systems over-engineers current delivery | Implement only CMake and current PackageProvider needs; define narrow interfaces and reject unimplemented adapters |
| Package lock becomes a duplicate composition lock | Lock acquired PackageInstances only; identify active semantic composition through the canonical graph fingerprint |
| A global lock cannot represent host/target or binary variants | Key entries by PackageInstance with context, derived BinaryCompatibility, and artifact-affecting Options |
| Binary packages are selected by insufficient compatibility | Derive BinaryCompatibility from Target, Toolchain ABI/runtime, Configuration, linkage, and relevant Options |
| Acquisition and capability terminology becomes ambiguous | Reserve PackageProvider for acquisition and use CapabilityImplementation/CapabilityBinding for semantic capability resolution |
| Paths make graphs machine-dependent or staging unsafe | Ratify bases, normalization, symlinks, globs, traversal, and portable serialization before resolution work |
| Temporary migration code becomes permanent | Keep it non-shipping, track deletion explicitly, and add old-syntax rejection tests |

## Definition of done

- [ ] One normative XML grammar covers Project, Package, and Workspace authoring.
- [ ] Authored manifests contain no public manifest format number before the
      official release.
- [ ] Projects declare one primary product directly with no product wrapper.
- [ ] Product Types exclude generic Module; reusable linked code uses Library,
      dynamically loaded products use Plugin, and C++ modules are build items.
- [ ] Semantic categories use documented category-specific merge laws.
- [ ] Fundamental selection is Configuration, Target, Toolchain, and typed
      Options; Presets do not enter graph identity.
- [ ] PackageCoordinate, PackageProviderResult, PackageInstance, and export
      activation are distinct.
- [ ] BinaryCompatibility is derived from Target, Toolchain, Configuration,
      linkage, and artifact-affecting Options rather than stored on Target.
- [ ] Multi-export packages activate only the selected/default export closure.
- [ ] Dependencies use testing/publishing context and typed export activation
      rather than compound Scopes, generic Optional, or generic Link flags.
- [ ] Version constraints require no escaped comparison operator.
- [ ] Generic Features are replaced by exports, Options, Assets, Actions,
      capabilities, Plugins, or required contributions.
- [ ] Every capability implementation has a Version, every requirement has a
      Version constraint, and resolution produces a CapabilityBinding.
- [ ] CapabilityBindings affect only acquisition, build, linking, generation,
      artifact selection, or deployment; runtime services are excluded.
- [ ] Required files/notices attach to package-level or export-level activation.
- [ ] Package-side generation/tooling uses one Action contract and project
      authoring retains typed verbs.
- [ ] Every exported Action references a semantic Tool export resolved on a
      host PackageInstance.
- [ ] Exported Actions require explicit activation and trust validation.
- [ ] Application runtime composition is owned by C++ and optional frameworks;
      manifests contain no Host, Module, runtime-service, DI, or lifecycle
      ordering concepts.
- [ ] Plugin exports describe build/deployment artifacts only and never direct
      runtime discovery, loading, or ordering.
- [ ] Semantic package metadata and graph types contain no CMake vocabulary or
      opaque backend data.
- [ ] CMakeIntegrationBindings are immutable, separate from the graph, and map
      semantic Product/Export/Tool identities to CMake representations.
- [ ] CMake integration is explicit, schema-defined, and the only implemented
      backend in this overhaul.
- [ ] PackageProvider, integration, artifact, and publisher boundaries can
      admit future adapters without changing ordinary dependency/export
      semantics.
- [ ] The Composition Graph is immutable semantic truth with provenance.
- [ ] Build, Action, Stage, Launch, Test, and Publish behavior comes from
      deterministic derived plans.
- [ ] No command rereads XML after graph construction.
- [ ] Path, glob, placeholder, canonicalization, and trust behavior is normative
      and tested.
- [ ] The dependency lock records exact acquired PackageInstances and only
      acquisition/artifact-affecting selection facts.
- [ ] The canonical Composition Graph produces a separate composition
      fingerprint covering active exports, Options, CapabilityBindings,
      Actions, Tools, Plugins, and deployment semantics.
- [ ] ManifestSpec drives structural XSD, editor metadata, CLI validation hooks,
      and reference vocabulary.
- [ ] Parser and XSD agree structurally; CLI semantic validation is complete.
- [ ] All repository manifests and positive fixtures use the new grammar.
- [ ] Old syntax is rejected and no production compatibility path remains.
- [ ] Canonical native, framework-based, and reflection projects validate
      successfully.
- [ ] Focused and final integration verification pass.
