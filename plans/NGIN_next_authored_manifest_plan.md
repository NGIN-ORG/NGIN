# NGIN Next Authored Manifest Plan

## Status

Proposed clean-sheet replacement for the current pre-release `.ngin`, `.nginproj`, and `.nginpkg` authoring grammar.

This plan deliberately breaks the current authored schema. It does not preserve superseded grammar in the main parser or resolver.

## Executive decision

NGIN should preserve its semantic architecture while replacing the authored surface.

The central model is:

- One `.nginproj` describes one build product.
- A build product is either an **Executable** or a **Library**.
- Application, Tool, Test, Benchmark, and Plugin are not peer product kinds.
- Test and Benchmark are executable registrations.
- Tool is a package export role for an executable.
- Plugin is a loadable Library kind and a package export role.
- NGIN.Core runtime module registration and lifecycle remain entirely in application code or application-owned configuration.

The authored XML lowers through an explicit internal boundary:

```text
Friendly .ngin* XML
        ↓
Normalized Manifest IR
        ↓
Resolved Composition Graph
        ↓
Build / Action / Stage / Run / Test / Benchmark / Publish plans
        ↓
CMake, Ninja, process launcher, test runner, packager
```

Authors describe intent. Manifest IR expands authoring conveniences into precise normalized facts. The Composition Graph remains the immutable resolved contract.

## Goals

The next schema must:

1. Match the normal C++ mental model of executables and libraries.
2. Avoid confusing purpose or usage role with physical artifact kind.
3. Make ordinary applications and libraries obvious at first reading.
4. Remove duplicated declarations, especially around generators and tools.
5. Keep build, package, deployment, and runtime-application concerns separate.
6. Make platform-specific additions readable without a general expression language.
7. Let projects require capabilities without naming concrete implementations unnecessarily.
8. Reduce workspace boilerplate through inspectable built-in defaults and discovery.
9. Use CPS for portable C/C++ package consumption rather than recreating it.
10. Preserve deterministic lowering, graph identity, provenance, diff, and explanation.
11. Remain useful without NGIN.Core.

## Non-goals

The next authored schema will not:

- Become a programming language.
- Contain arbitrary boolean expressions, loops, functions, or imports.
- Treat Application, Tool, Test, Benchmark, and Module as unrelated build artifacts.
- Describe dependency injection, runtime service registration, or application lifecycle.
- Automatically register or start NGIN.Core modules.
- Encode CMake target names or cache variables in the Composition Graph.
- Replace CPS as the portable description of installed C/C++ components.
- Preserve the old grammar through permissive parsing.
- Make XML ordering select a dependency, capability provider, or conflict winner.

## Design rationale: artifact versus role

The previous proposal used six project roots:

- Application
- Library
- Tool
- Test
- Benchmark
- Module

That mixes different axes:

| Concept | What it actually describes |
| --- | --- |
| Application | How an executable is used |
| Tool | How an executable is consumed by another build or user |
| Test | How a command is registered with a test runner |
| Benchmark | How a command is registered with a benchmark runner |
| Module | An overloaded term for C++ modules, loadable libraries, or NGIN.Core runtime modules |
| Library | A physical build artifact |

The C++ ecosystem generally builds executables and libraries, then attaches other behavior separately:

- CMake has `add_executable` and `add_library`; `add_test` registers a command that may reference an executable.
- Meson creates executables and libraries, then registers executables with `test()` or `benchmark()`.
- CMake `MODULE` libraries are plugin artifacts loaded dynamically rather than a general application module model.

NGIN should follow that separation.

## Core product model

A `.nginproj` has exactly one of two roots:

- `<Executable>`
- `<Library>`

### Executable

An Executable can be:

- Run directly.
- Staged and published as an application.
- Exported from a package as a Tool.
- Registered as one or more tests.
- Registered as one or more benchmarks.
- Used by a generator or other package action.

None of these usages change the product's physical artifact kind.

### Library

A Library has a physical kind:

- `Static`
- `Shared`
- `Interface`
- `Plugin`

`Plugin` means a loadable library artifact that is not intended for ordinary link consumption. It maps naturally to backend concepts such as a CMake `MODULE` library and a CPS module component.

`Object` is intentionally excluded from the initial public product model. Object libraries are build-implementation details rather than stable consumable products. They can be added later if a real NGIN use case requires them.

### Removed project kinds

| Current or previously proposed kind | Next representation |
| --- | --- |
| Application | Executable, optionally with Run/Stage/Publish intent |
| Tool | Executable exported as a package Tool |
| Test | Executable with one or more Test registrations |
| Benchmark | Executable with one or more Benchmark registrations |
| Plugin | Library with `Kind="Plugin"`, optionally exported as a package Plugin |
| Module | Removed as a project kind |
| External | Package/provider integration |

The word `Module` is reserved for explicit contexts:

- `<CxxModule>` is a C++ language build item.
- NGIN.Core runtime modules are application concepts expressed in C++ or application configuration.
- Dynamically loadable build artifacts use `Library Kind="Plugin"`.

## Document roles

### `.nginproj`: one source product

A `.nginproj` describes one Executable or Library built from source.

It may also declare:

- What the product uses.
- Build inputs and usage requirements.
- Selected generators and developer tooling.
- Conditional additions.
- Staging and publishing intent.
- Run definitions for an Executable.
- Test and Benchmark registrations for an Executable.

It does not describe runtime service registration or NGIN.Core lifecycle.

### `.nginpkg`: package semantics and overlays

A `.nginpkg` describes a released or acquired package when NGIN-specific semantics are needed. Primary uses include:

- Multi-component packages.
- External packages without sufficient portable metadata.
- NGIN capabilities.
- Executables exposed as host Tools.
- Package actions such as generators, analyzers, and formatters.
- Loadable libraries exposed as Plugins.
- Assets, notices, and staging contributions.
- NGIN-specific overlays on CPS components.
- Source-package adapter metadata when no better provider-native integration exists.

Ordinary single-product NGIN libraries and executables should not require a parallel handwritten `.nginpkg`. NGIN should generate package metadata and CPS descriptions during publish.

### `.ngin`: workspace policy and selection

A workspace:

- Discovers projects and package overlays.
- Defines shared version policy.
- Defines non-standard configurations, targets, and toolchains when needed.
- Provides named profiles.
- Selects preferred capability implementations.
- Owns trust and reproducibility policy.

It does not repeat project behavior or become another build language.

## Project grammar

### Executable root

```xml
<?xml version="1.0" encoding="utf-8"?>
<Executable Name="Hello.Native" Version="0.1.0">
  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
</Executable>
```

An Executable has an implicit default Run definition targeting itself. It does not need to be labelled Application or Tool.

### Library root

```xml
<?xml version="1.0" encoding="utf-8"?>
<Library Name="NGIN.Example.Math"
         Version="1.0.0"
         Kind="Static">
  <Build>
    <Language Standard="C++23" Extensions="false" />
    <Source Include="src/**/*.cpp" />
    <Header Include="include/**/*.hpp" Visibility="Public" />
    <IncludeDirectory Path="include" Visibility="Public" />
  </Build>
</Library>
```

Structural validation can enforce that:

- Only a Library has `Kind` and public link usage requirements.
- Only an Executable has Run, Test, and Benchmark registrations.
- An Interface Library cannot contain compiled sources.
- A Plugin Library cannot be selected as an ordinary link Library export.

## Uses

`<Uses>` replaces project `<Dependencies>` and friendly package/export `<Requires>`.

```xml
<Uses>
  <Package Name="NGIN.ECS" Version="0.4" />
  <Project Path="../Shared/Shared.nginproj" />
  <Capability Name="NGIN.UI.Backend" Version="1" />
</Uses>
```

The Manifest IR still contains typed requirement, propagation, activation, and host/target edges.

### Version requests

- `Version="0.4"` is a compatibility request.
- `Exact="0.4.7"` requests one exact version and is intentionally uncommon.
- A structured `<Version>` supports bounded intervals.
- Exact provider results belong in the dependency lock.
- Capability `Version` describes the capability contract, not the provider package version.

```xml
<Package Name="Example.Security">
  <Version AtLeast="3.2.0" Before="4.0.0" />
</Package>
```

### Export selection

Omitting children selects the package's default exports:

```xml
<Package Name="Example.Security" Version="3.2" />
```

Explicit selection remains typed:

```xml
<Package Name="Example.Security" Version="3.2">
  <Library Name="TLS" />
</Package>
```

Selecting a Plugin only makes its artifact and contributions available:

```xml
<Package Name="NGIN.Diagnostics" Version="0.1">
  <Plugin Name="Diagnostics" />
</Package>
```

It does not load or register the Plugin at runtime.

## Build

`<Build>` remains because it is direct, familiar, and semantically valuable.

```xml
<Build>
  <Language Standard="C++23" Extensions="false" />
  <Source Include="src/**/*.cpp" />
  <Header Include="include/**/*.hpp" Visibility="Public" />
  <IncludeDirectory Path="include" Visibility="Public" />
  <Define Name="MATH_BUILDING" Visibility="Private" />
</Build>
```

Rules:

- Build items remain typed and additive.
- `Include`, `Exclude`, `Remove`, and `Update` remain explicit operations.
- Lists are repeated typed elements, never semicolon-delimited strings.
- Paths remain manifest-relative and portable.
- `<CxxModule>` describes a C++ language module source file only.
- Conventions may provide defaults, but every inferred item must appear in `ngin inspect --effective` with provenance.
- Initial migration retains explicit source declarations. Broader source conventions can follow real usage data.

## Conditions

Replace `<Refinements><Refinement><Select>` with one constrained `<When>` block.

```xml
<When OS="windows">
  <Uses>
    <Package Name="NGIN.UI.Accessibility.Windows" Version="0.4" />
  </Uses>
</When>

<When Configuration="Debug">
  <Build>
    <Define Name="NGIN_ENABLE_ASSERTS" />
  </Build>
</When>

<When OS="windows" Architecture="x64">
  <Stage>
    <File From="native/windows-x64/helper.dll" To="bin/helper.dll" />
  </Stage>
</When>
```

Rules:

- Selectors are typed: Configuration, Target, OS, Architecture, Toolchain, Compiler, and declared Option values.
- Attributes in one block are ANDed.
- Multiple blocks express alternative additive cases.
- Nested `<When>` blocks are prohibited.
- No arbitrary expression string exists.
- Every matching block contributes.
- There is no specificity winner or priority.
- Conflicting keyed writes are errors.
- Replacement or removal is explicit.

Projects, packages, and backend adapters share this limited condition vocabulary.

## Generation and tooling

Selecting an action is itself a host dependency on its package, action export, and backing Tool.

```xml
<Generate Using="NGIN.Reflection.MetaGen/Reflection" Version="0.1">
  <Header Include="src/**/*.hpp" />
</Generate>
```

This lowers into:

- A host-context package requirement.
- Generator/action activation.
- Backing Tool activation.
- Workspace action-trust evaluation.
- An ActionPlan.
- Generated build items.

The project does not separately list `NGIN.Reflection.MetaGen` under `<Uses>` merely to activate the generator.

Tooling follows the same rule:

```xml
<Tooling>
  <Analyze Using="NGIN.Tooling.ClangTidy/Analyze" />
  <Format Using="NGIN.Tooling.ClangFormat/Format" />
</Tooling>
```

Package authors receive meaningful action elements:

- `<Generator>`
- `<Analyzer>`
- `<Formatter>`
- `<Validator>`
- `<Action>` for genuinely custom verbs

These lower to the generic Action and Tool model in Manifest IR.

## Run definitions

Every Executable has an implicit default Run definition targeting itself.

This is runnable without additional XML:

```xml
<Executable Name="Hello.Native">
  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
</Executable>
```

Add `<Run>` only for customization:

```xml
<Run WorkingDirectory="content">
  <Argument>--development</Argument>
  <Environment Name="LOG_LEVEL" Value="debug" />
</Run>
```

Multiple run definitions are allowed:

```xml
<Run Name="client" Default="true">
  <Argument>--client</Argument>
</Run>

<Run Name="server">
  <Argument>--server</Argument>
</Run>
```

Rules:

- A single explicit Run is automatically the default.
- `Default="true"` is needed only to disambiguate several Runs.
- The product itself is implicit unless a package Tool is explicitly selected.
- Secret values remain external references and never enter graph identity or generated files.

## Test registrations

A Test is an execution registration attached to an Executable, not a product kind.

```xml
<Executable Name="NGIN.UI.Tests">
  <Uses>
    <Project Path="../NGIN.UI/NGIN.UI.nginproj" />
    <Package Name="Catch2" Version="3.5" />
  </Uses>

  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>

  <Test Timeout="60" />
</Executable>
```

One Executable may have multiple registrations:

```xml
<Test Name="unit" />

<Test Name="integration" Timeout="120">
  <Argument>--integration</Argument>
  <Environment Name="TEST_DATA" Value="${stage.root}/test-data" />
</Test>
```

`ngin run NGIN.UI.Tests` runs the executable directly. `ngin test` executes its Test registrations through the test runner with timeout, environment, protocol, and reporting semantics.

Test dependencies are ordinary dependencies of the test Executable. A separate `<Testing><Dependencies>` hierarchy is unnecessary.

## Benchmark registrations

A Benchmark is also an execution registration attached to an Executable:

```xml
<Executable Name="NGIN.Base.Benchmarks">
  <Uses>
    <Project Path="../NGIN.Base/NGIN.Base.nginproj" />
    <Package Name="GoogleBenchmark" Version="1.9" />
  </Uses>

  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>

  <Benchmark />
</Executable>
```

BenchmarkPlan semantics include:

- Benchmarks are not run in parallel by default.
- Benchmark filters, repetitions, warm-up, and output artifacts are typed settings.
- Timing variation is not treated like an ordinary test assertion failure.
- `ngin benchmark` is distinct from `ngin test`.
- The underlying build product remains an Executable.

## Stage

`<Stage>` describes deployment inputs:

```xml
<Stage>
  <File From="config/app.cfg" To="config/app.cfg" />
  <Directory From="content" To="content" />
</Stage>
```

Rules:

- Project and activated package contributions enter one normalized StagePlan.
- Collisions are errors unless workspace policy explicitly permits identical bytes.
- Staging a Plugin makes the artifact available but does not load it.

## Publish

Publish intent remains backend-neutral:

```xml
<Publish>
  <Archive Name="demo"
           Format="zip"
           Output="dist/${project.name}-${project.version}.zip" />

  <Installer Name="windows"
             Format="msi"
             Output="dist/${project.name}-${project.version}.msi" />
</Publish>
```

Publishing derives a PublishPlan from the resolved product and staged layout. It does not execute arbitrary package scripts.

## Runtime boundary

### Decision

Runtime module registration and lifecycle do not belong in `.ngin*` authoring.

There is no project-level construct such as:

```xml
<Runtime>
  <Module Package="NGIN.Diagnostics" />
</Runtime>
```

An application using NGIN.Core owns runtime composition in C++ or application-owned configuration.

Illustrative code:

```cpp
int main()
{
    NGIN::Host Host;
    Host.Use<DiagnosticsModule>();
    Host.Use<TelemetryModule>();
    return Host.Run();
}
```

The final NGIN.Core API is outside this schema plan, but the ownership boundary is fixed.

### What manifests may describe

Manifests may describe deployable artifacts:

- A loadable Plugin library.
- Its ABI and target compatibility.
- Files staged with it.
- Packages required to build or deploy it.
- Build or deployment capabilities provided by it.
- Provenance and integrity.

Selecting a Plugin means “make this artifact available,” never “load, configure, and start it.”

### Composition Graph consequences

Keep:

- Executable and Library products.
- Plugin Library kind.
- Package Plugin exports.
- Artifact/package dependencies.
- Runtime file contributions.
- Staging edges and provenance.
- ABI and platform compatibility.

Exclude:

- Runtime module registration nodes.
- Startup ordering.
- Service registration.
- Dependency injection configuration.
- Automatic module discovery or loading semantics.
- Module start/stop lifecycle.
- Application framework configuration.

NGIN.Core remains an ordinary optional dependency. Plain NGIN-built C++ programs remain independent of it.

## Package grammar

### Direct typed exports

Remove the `<Exports>` wrapper. Typed consumables appear directly under `<Package>`:

```xml
<Package Name="Example.Security"
         Version="3.2.1"
         CompatibleSince="3.0.0">
  <Library Name="Crypto" Default="true">
    <Provides Name="NGIN.Crypto" Version="1" />
  </Library>

  <Library Name="TLS">
    <Uses>
      <Library Name="Crypto" Public="true" />
    </Uses>

    <Provides Name="NGIN.Net.TLS" Version="1" />

    <RuntimeFiles>
      <File From="bin/tls-runtime.*" To="bin/" />
    </RuntimeFiles>
  </Library>
</Package>
```

Supported direct package exports include:

- `<Library>`
- `<Tool>`
- `<Plugin>`
- `<Generator>`
- `<Analyzer>`
- `<Formatter>`
- `<Validator>`
- `<Action>`
- `<Asset>`

These are package consumption roles, not `.nginproj` product roots.

Rules:

- One export is implicitly the default.
- Multi-export packages explicitly identify their default export set.
- Typed child elements replace a generic union-shaped `<Use>`.
- Local export dependencies use typed children inside `<Uses>`.
- Public dependency propagation is explicit.
- Activation still resolves to a fixed point in Manifest IR and the Composition Graph.

### Exporting an Executable as a Tool

An Executable becomes a package Tool through package metadata:

```xml
<Tool Name="MetaGen" Product="MetaGen" />
```

The same Executable can still be run directly. “Tool” describes how a package consumer uses it and causes host-context acquisition when selected by an action.

For ordinary single-product NGIN projects, the package/export metadata should be generated during publish from a concise project publish declaration rather than maintained in a second file.

### Exporting a Plugin Library

A source project builds the physical artifact:

```xml
<Library Name="NGIN.Diagnostics" Kind="Plugin">
  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
</Library>
```

Package metadata exposes it for deployment:

```xml
<Plugin Name="Diagnostics" Product="NGIN.Diagnostics">
  <RuntimeFiles>
    <File From="config/diagnostics.xml" To="config/diagnostics.xml" />
  </RuntimeFiles>
</Plugin>
```

The export causes artifact activation and staging only.

### Options

Typed options remain:

```xml
<Options>
  <Boolean Name="Reflection" Default="false" />

  <Enum Name="Allocator" Default="System">
    <Value Name="System" />
    <Value Name="Mimalloc" />
  </Enum>
</Options>
```

Changes:

- Package options affect artifact identity by default.
- An option that does not affect artifact identity declares `Artifact="false"`.
- Backend adapters cannot repeat or override this semantic decision.
- Option predicates use the constrained `<When Option="..." Equals="...">` mechanism.

### Capabilities

Projects and packages can require capabilities:

```xml
<Capability Name="NGIN.Net.TLS" Version="1" />
```

Package exports provide them:

```xml
<Provides Name="NGIN.Net.TLS" Version="1" />
```

Rules:

- `Version` is the capability contract version.
- Semantic domain is inferred from the owning export when unambiguous.
- Explicit `Domain` remains available for advanced cases.
- Resolution must produce exactly one compatible implementation.
- Zero and multiple compatible implementations are errors.
- Projects may constrain `Provider`, but workspace preferences are preferred for shared defaults.
- Capabilities do not model runtime services, NGIN.Core modules, or dependency injection.

### Actions

Meaningful action elements lower to a generic normalized Action:

```xml
<Tool Name="MetaGen" Product="MetaGen" />

<Generator Name="Reflection"
           Tool="MetaGen"
           Deterministic="true">
  <Inputs>
    <Header Include="**/*.hpp" />
  </Inputs>

  <Outputs>
    <Source Path="generated/reflection.generated.cpp" />
  </Outputs>
</Generator>
```

Action definitions do not execute merely because their package is present. A project verb must select them.

## CPS boundary

CPS is the preferred portable contract for compiled package components.

NGIN imports CPS into its normalized package model when available. A `.nginpkg` overlays CPS rather than duplicating compiled components and usage requirements.

```xml
<Package Name="OpenSSL" Version="3.5.2">
  <Import Cps="OpenSSL.cps" />

  <Capabilities>
    <Provide Name="NGIN.Crypto"
             Version="1"
             Component="OpenSSL:Crypto" />

    <Provide Name="NGIN.Net.TLS"
             Version="1"
             Component="OpenSSL:SSL" />
  </Capabilities>
</Package>
```

| Concern | Owner |
| --- | --- |
| Compiled Executable, Library, and module components | CPS |
| Portable usage requirements | CPS |
| Acquired component locations | CPS/provider result |
| Package acquisition and integrity | PackageProvider |
| NGIN capability mapping | `.nginpkg` overlay |
| Generator invocation contracts | `.nginpkg` |
| Assets, notices, and staging contributions | `.nginpkg` |
| Runtime application registration | Application code/NGIN.Core |
| Exact normalized composition | Composition Graph |

NGIN-native projects emit CPS during publish. Generated CPS and NGIN package metadata are outputs, not parallel handwritten sources of truth.

## Backend adapters

Backend-specific details remain outside package semantics and Composition Graph identity.

Retain XML namespaces for strong backend-specific validation:

```xml
<Adapters xmlns:cmake="urn:ngin:adapter:cmake">
  <cmake:AddSubdirectory Source=".">
    <cmake:Target Export="UI" Name="NGIN::UI" />
  </cmake:AddSubdirectory>
</Adapters>
```

Changes:

- Rename `<Integrations>` to `<Adapters>`.
- Adapter metadata lowers into an immutable sidecar keyed by semantic identities.
- Adapter fields never enter Composition Graph identity.
- Adapters cannot redefine option artifact identity.
- Prefer CPS import over CMake `find_package` mapping when CPS exists and policy permits it.
- Do not introduce a separate adapter file until real package complexity justifies it.

## Workspace grammar

### Built-in defaults

The CLI provides named, inspectable defaults:

- `Debug`: optimization off, debug symbols enabled.
- `Release`: speed optimization, debug symbols disabled, LTO enabled when supported.
- Target `host`: current host OS and architecture.
- Toolchain `auto`: selected from host and requested target.
- Output root `.ngin/build`.
- Path containment required.
- Symlinks rejected unless explicitly allowed.
- Stage collisions are errors.
- External actions require trust evaluation.

Defaults appear in `ngin inspect --effective` with built-in provenance and can be overridden explicitly.

### Discovery

```xml
<Workspace Name="NGIN">
  <Discover>
    <Projects Include="Examples/**/*.nginproj" />
    <Projects Include="Tools/**/*.nginproj" />
    <Packages Include="Packages/**/*.nginpkg" />
  </Discover>
</Workspace>
```

Rules:

- An exact project/package path is an Include without glob characters.
- Overlapping declarations resolving to the same normalized file are deduplicated with a diagnostic.
- A local package root defaults to the manifest directory.
- Per-package `<LocalPackage Name="..." Manifest="..." Root="..." />` declarations are removed.
- Results remain normalized, contained, sorted, and deterministic.

### Versions

```xml
<Versions>
  <Package Name="NGIN.Base" Version="0.1" />
  <Package Name="NGIN.Core" Version="0.1" />
  <Package Name="NGIN.ECS" Version="0.4" />
  <Package Name="NGIN.UI" Version="0.4" />
</Versions>
```

Central versions constrain requests but do not create dependencies. Unused entries produce warnings rather than hard errors.

### Profiles

Profiles replace the authoring overlap between Configurations, Defaults, and Presets:

```xml
<Profiles Default="dev">
  <Profile Name="dev" Configuration="Debug" />
  <Profile Name="release" Configuration="Release" />
</Profiles>
```

Profiles may select:

- Configuration.
- Target.
- Toolchain.
- Project/package options.
- A named Run definition.

The profile name does not enter Composition Graph identity. Only resolved facts are semantic.

Targets and toolchains remain separate because destination platform and compiler/ABI identity are distinct axes.

### Capability preferences

```xml
<Capabilities>
  <Prefer Name="NGIN.UI.Backend"
          Provider="NGIN.UI.Backend.SDL3" />

  <When OS="windows">
    <Prefer Name="NGIN.Crypto"
            Provider="NGIN.Crypto.CNG" />
  </When>
</Capabilities>
```

Preferences filter compatible candidates. They cannot make an incompatible implementation valid.

### Trust

Policies express exceptions and gates rather than restating safe defaults:

```xml
<Trust>
  <AllowActions From="Packages/NGIN.*"
                Reason="First-party workspace tools" />
</Trust>
```

The normalized policy model retains provider kind, integrity, locked mode, signatures, action kind, and provenance.

## Complete examples

### Ordinary executable application

```xml
<?xml version="1.0" encoding="utf-8"?>
<Executable Name="Hello.GameOfLife" Version="0.1.0">
  <Uses>
    <Package Name="NGIN.ECS" Version="0.4" />
    <Package Name="NGIN.UI" Version="0.4" />
    <Capability Name="NGIN.UI.Backend" Version="1" />
  </Uses>

  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>

  <Stage>
    <File From="../../LICENSE" To="licenses/NGIN/LICENSE" />
  </Stage>

  <When OS="windows">
    <Uses>
      <Package Name="NGIN.UI.Accessibility.Windows" Version="0.4" />
    </Uses>
  </When>
</Executable>
```

### Reflection executable

```xml
<?xml version="1.0" encoding="utf-8"?>
<Executable Name="Hello.Reflection">
  <Uses>
    <Package Name="NGIN.Reflection" Version="0.1" />
  </Uses>

  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>

  <Generate Using="NGIN.Reflection.MetaGen/Reflection" Version="0.1">
    <Header Include="src/**/*.hpp" />
  </Generate>
</Executable>
```

### Test executable

```xml
<?xml version="1.0" encoding="utf-8"?>
<Executable Name="NGIN.UI.Tests">
  <Uses>
    <Project Path="../NGIN.UI/NGIN.UI.nginproj" />
    <Package Name="Catch2" Version="3.5" />
  </Uses>

  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>

  <Test Timeout="60" />
</Executable>
```

### Benchmark executable

```xml
<?xml version="1.0" encoding="utf-8"?>
<Executable Name="NGIN.Base.Benchmarks">
  <Uses>
    <Project Path="../NGIN.Base/NGIN.Base.nginproj" />
    <Package Name="GoogleBenchmark" Version="1.9" />
  </Uses>

  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>

  <Benchmark />
</Executable>
```

### Plugin library

```xml
<?xml version="1.0" encoding="utf-8"?>
<Library Name="NGIN.Diagnostics"
         Version="0.1.0"
         Kind="Plugin">
  <Uses>
    <Package Name="NGIN.Log" Version="0.1" />
  </Uses>

  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
</Library>
```

### Workspace

```xml
<?xml version="1.0" encoding="utf-8"?>
<Workspace Name="NGIN">
  <Discover>
    <Projects Include="Examples/**/*.nginproj" />
    <Projects Include="Tools/**/*.nginproj" />
    <Packages Include="Packages/**/*.nginpkg" />
  </Discover>

  <Versions>
    <Package Name="NGIN.Base" Version="0.1" />
    <Package Name="NGIN.Core" Version="0.1" />
    <Package Name="NGIN.ECS" Version="0.4" />
    <Package Name="NGIN.UI" Version="0.4" />
  </Versions>

  <Profiles Default="dev">
    <Profile Name="dev" Configuration="Debug" />
    <Profile Name="release" Configuration="Release" />
  </Profiles>

  <Capabilities>
    <Prefer Name="NGIN.UI.Backend"
            Provider="NGIN.UI.Backend.SDL3" />
  </Capabilities>

  <Trust>
    <AllowActions From="Packages/NGIN.*"
                  Reason="First-party workspace tools" />
  </Trust>
</Workspace>
```

## Manifest IR

### Purpose

Manifest IR is the normalization boundary between friendly authoring and semantic resolution. It is internal and never authored directly.

It makes all conveniences explicit:

- Product artifact kind: Executable or Library.
- Library kind: Static, Shared, Interface, or Plugin.
- Package, project, and capability requirements.
- Version constraints and export activations.
- Host versus target context.
- Expanded conventions and matching `<When>` blocks.
- Action package and Tool dependencies.
- Implicit Run definitions.
- Test and Benchmark registrations.
- Typed Stage and Publish contributions.
- Package/CPS component mappings.
- Source provenance for every lowered fact.

### Composition Graph changes

The resolved product model records:

- `artifactKind`: Executable or Library.
- `libraryKind` when applicable.
- Build inputs and usage requirements.
- Run definitions.
- Test registrations.
- Benchmark registrations.

Package exports remain separate semantic nodes:

- A Tool export points to or represents an executable component.
- A Plugin export points to or represents a loadable library component.
- An Action refers to a Tool export.

No graph node claims that an Executable is intrinsically an Application, Tool, Test, or Benchmark. Those are uses or registrations.

### Lowering guarantees

Lowering is:

- Deterministic.
- Independent of XML order except where order is semantic, such as arguments.
- Fully source-located.
- Strict about duplicate identities and conflicting writes.
- Inspectable through a canonical effective-manifest representation.
- Free of backend-specific CMake facts.

## Schema and versioning

NGIN is pre-release, so the Next grammar replaces the old grammar without a public `SchemaVersion` attribute.

Before the first stable release:

- The parser, generated XSD, tests, canonical examples, and reference documentation define the grammar.
- Breaking changes update all authored files together.
- No fallback accepts removed elements.

At the first stable authoring contract:

- Introduce a major-versioned default XML namespace.
- Increment the namespace only for incompatible grammar changes.
- Keep additive evolution within a major namespace where unknown semantics can be rejected safely.

## Tooling requirements

### `ngin validate`

Must provide:

- Structural and semantic validation.
- Exact file, line, and column.
- Unknown-element and unknown-attribute suggestions.
- Product-specific diagnostics, such as Test under Library.
- Ambiguous capability candidate lists.
- Conflicting condition diagnostics showing both sources.
- Clear distinction between authored, built-in, package, provider, and adapter facts.

### `ngin format`

Must provide canonical stable XML formatting:

- Consistent indentation.
- Predictable attribute ordering.
- No semantic reordering of arguments.
- Stable repeated output.
- `--check` mode for CI.

### `ngin inspect --effective`

Shows Manifest IR before package resolution, including:

- Built-in defaults.
- Expanded conventions.
- Applied `<When>` blocks.
- Implicit Run definitions.
- Test and Benchmark registrations.
- Host dependencies introduced by actions.
- Source provenance.

### `ngin graph`, `diff`, and `explain`

- `graph` emits canonical Composition Graph JSON.
- `diff` compares semantic identities rather than XML formatting.
- `explain` traces facts through Manifest IR, package resolution, capability binding, or built-in convention.

### Editor integration

Generate from ManifestSpec:

- XSD for all authored document types.
- Separate root content models for Executable and Library.
- Editor metadata and completion descriptions.
- Attribute value completions.
- Package, capability, export, action, profile, target, and toolchain reference completions.
- Quick fixes for constructs moved during the repository cutover.

## Implementation plan

### Phase 0 — Freeze decisions and examples

Deliverables:

- Approve Executable and Library as the only project roots.
- Approve Library kinds: Static, Shared, Interface, Plugin.
- Approve Test and Benchmark as executable registrations.
- Approve Tool and Plugin as package export roles.
- Approve removal of Module as a project root.
- Commit canonical Next examples for Executable, Library, Test registration, Benchmark registration, Plugin Library, generator use, capability use, platform conditions, CPS overlay, and workspace.
- Record the runtime boundary in an architecture decision.

Exit criteria:

- Every major concept has a small example.
- Artifact kinds and usage roles are never conflated.
- No example requires a general expression language.
- Plugin availability is clearly separate from runtime activation.

### Phase 1 — Introduce Manifest IR

Deliverables:

- Add immutable Manifest IR model types.
- Normalize current project kinds into artifact kinds and registrations.
- Add stable identities and provenance for normalized authored facts.
- Refactor the resolver to consume Manifest IR rather than authored XML nodes.
- Temporarily keep the current parser only as an internal producer of Manifest IR.

Exit criteria:

- Current canonical examples resolve through Manifest IR.
- Existing Composition Graph fingerprints remain stable where semantics are unchanged.
- Resolver and plan code cannot access authored XML after the Manifest IR boundary.

### Phase 2 — Implement the Next project parser

Deliverables:

- Executable and Library roots.
- Library Kind validation.
- `<Uses>` package/project/capability grammar.
- Typed `<Build>` items.
- `<When>` lowering.
- Generator/tooling selection as host dependencies.
- Implicit and explicit Run definitions.
- Test and Benchmark registrations.
- Simplified Stage and Publish grammar.
- Generated project XSD and editor metadata.

Exit criteria:

- Hello.Native, Hello.Reflection, Hello.Hosted, GameOfLife, UI Gallery, Tests, and Benchmarks validate with the Next grammar.
- Hello.Reflection has no duplicate MetaGen activation.
- UI platform additions use `<When OS="windows">`.
- Executables without explicit Run definitions can build, stage, and run.
- Test and Benchmark registrations derive distinct plans from ordinary Executable products.
- A Library rejects Run, Test, and Benchmark children structurally.

### Phase 3 — Implement the Next package parser

Deliverables:

- Direct typed exports without `<Exports>`.
- Automatic default for single-export packages.
- Typed local export dependencies through `<Uses>`.
- Project and package capability requirements.
- Capability domain inference.
- Meaningful Action types lowering to generic actions.
- Tool exports backed by executable products/components.
- Plugin exports backed by Plugin libraries/components.
- Runtime-file semantics without runtime activation.
- Option artifact identity defaulting.
- Renamed `<Adapters>` surface.

Exit criteria:

- NGIN.Base, NGIN.Core, NGIN.Reflection.MetaGen, NGIN.UI, SDL3, OpenSSL, and a multi-export fixture resolve correctly.
- Activating a Plugin stages files but produces no runtime registration or lifecycle plan.
- Actions remain inert until selected by a project verb.
- Selecting an Action resolves its Tool in host context.

### Phase 4 — Add CPS consumption and emission

Deliverables:

- CPS parser/provider normalization.
- Mapping of CPS executable, library, interface, and module components.
- `.nginpkg` CPS overlays.
- CPS-first selection when CPS and CMake package metadata both exist and policy permits it.
- CPS generation for publishable NGIN Executable and Library products.
- Component/export identity mapping with provenance.

Exit criteria:

- CPS components satisfy normal `<Package>` dependencies.
- A CPS executable can back a Tool export.
- A CPS module component can back a Plugin export.
- A `.nginpkg` overlay can attach NGIN capabilities.
- NGIN products publish CPS without separately authored component metadata.
- Generated CPS round-trips through the importer for supported concepts.

### Phase 5 — Implement the Next workspace parser

Deliverables:

- Simplified `<Discover>`.
- Built-in Debug, Release, host, auto-toolchain, and output defaults.
- Automatic local package root inference.
- `<Versions>` central policy.
- Profiles replacing Defaults/Presets overlap.
- Capability preferences.
- Simplified trust rules lowering to the strict policy model.
- Effective workspace inspection with built-in provenance.

Exit criteria:

- The NGIN workspace no longer lists every local package manually.
- Ordinary host Debug/Release builds need no Target or Toolchain declaration.
- Equivalent profile selections produce identical graphs regardless of profile name.
- Capability preferences remain deterministic and compatibility-safe.

### Phase 6 — Migrate the repository

Migration order:

1. Canonical fixtures and parser tests.
2. Hello.Native and ordinary Executables.
3. Hello.Reflection and action/tool packages.
4. Hello.Hosted without manifest-owned runtime activation.
5. Test and Benchmark executables.
6. Plugin Library products and Plugin package exports.
7. NGIN.UI examples and platform conditions.
8. First-party package manifests.
9. Third-party package wrappers.
10. Root workspace.
11. Documentation and VS Code extension fixtures.

For each migrated manifest:

- Validate structural lowering.
- Compare Manifest IR with expected normalized facts.
- Compare old/new Composition Graphs when semantics should remain equivalent.
- Explain every intended semantic difference.
- Run the narrowest applicable build, stage, run, test, or benchmark flow.

Exit criteria:

- No current-schema authored manifests remain.
- Canonical examples demonstrate only the Next grammar.
- Documentation no longer describes Application, Tool, Test, Benchmark, Module, or External as product kinds.
- Runtime module activation appears only in application/NGIN.Core documentation.

### Phase 7 — Remove the current grammar

Deliverables:

- Delete current parser routes and semantic fallbacks.
- Delete current generated schemas.
- Remove compatibility tests expecting superseded elements.
- Reject old roots and elements with precise diagnostics.
- Retain no automatic migration in validate/build flows.

An optional one-time migration utility may be developed separately, but it must emit Next manifests and must not become a permanent compatibility layer.

Exit criteria:

- One parser and one authored grammar remain.
- Removed elements fail explicitly.
- No permissive path silently accepts old semantics.

### Phase 8 — Authoring-quality release gate

Before declaring the schema ready:

- Test empty-document autocomplete for Executable and Library.
- Review ordinary application, library, test, benchmark, generator, plugin, and platform-specific scenarios with fresh users.
- Verify common errors produce actionable diagnostics.
- Verify `ngin format` is stable.
- Verify `ngin inspect --effective` explains every convenience and default.
- Verify graph serialization remains deterministic.
- Verify lock and composition fingerprints remain distinct.
- Verify plain Executables have no NGIN.Core requirement.
- Verify Plugin staging never implies Plugin loading.
- Verify Test and Benchmark are registrations rather than product kinds at every model layer.

## Migration mapping

| Current | Next |
| --- | --- |
| `<Project Type="Application">` | `<Executable>` |
| `<Project Type="Tool">` | `<Executable>`; package `<Tool>` when exported |
| `<Project Type="Test">` plus `<Testing>` | `<Executable>` plus `<Test>` registration |
| `<Project Type="Benchmark">` | `<Executable>` plus `<Benchmark>` registration |
| `<Project Type="Library" Linkage="Static">` | `<Library Kind="Static">` |
| `<Project Type="Library" Linkage="Shared">` | `<Library Kind="Shared">` |
| `<Project Type="Library" Linkage="Interface">` | `<Library Kind="Interface">` |
| `<Project Type="Plugin">` | `<Library Kind="Plugin">` |
| `<Project Type="External">` | Package/provider integration |
| `<Dependencies>` | `<Uses>` |
| `<Package Compatible="0.4">` | `<Package Version="0.4">` |
| `<Use Library="TLS" />` | `<Library Name="TLS" />` |
| Dependency Action selection plus `<Generate Action="...">` | One `<Generate Using="Package/Generator">` |
| `<Refinements><Refinement><Select>...` | `<When ...>` |
| `<Launch Name="Default" Default="true">` | Implicit Run or `<Run>` |
| `<Testing><Timeout Seconds="60" /></Testing>` | `<Test Timeout="60" />` |
| Benchmark product kind | `<Benchmark>` registration |
| `<Stage><File Include="..." Into="..." />` | `<Stage><File From="..." To="..." />` |
| `<Exports><Library ... /></Exports>` | Direct package `<Library>` export |
| `<Action Kind="Generate">` | `<Generator>` |
| `<Action Kind="Analyze">` | `<Analyzer>` |
| `<Integrations>` | `<Adapters>` |
| Per-package `<LocalPackage ... />` | Package discovery and inferred root |
| `<Defaults>` plus `<Presets>` | `<Profiles>` plus built-in defaults |
| Authored runtime module activation | Application code/NGIN.Core |

## Testing strategy

### Parser and schema tests

- Valid and invalid Executable and Library roots.
- Library Kind restrictions.
- Structural rejection of Run, Test, or Benchmark under Library.
- Structural rejection of compiled sources under Interface Library.
- Unknown-element and unknown-attribute suggestions.
- Version request validation.
- Reference completion metadata.

### Lowering tests

- Friendly syntax lowers to exact Manifest IR facts.
- Implicit Run lowering.
- Test and Benchmark registration lowering.
- Action selection introduces correct host dependencies.
- Matching `<When>` blocks contribute additively.
- Conflicts fail with both source locations.
- Built-in defaults have explicit provenance.
- Plugin selection creates staging facts and no runtime activation facts.

### Semantic resolution tests

- Default and explicit export activation.
- Capability resolution from projects and packages.
- Workspace capability preference.
- Host/target package separation.
- Tool export backed by Executable.
- Plugin export backed by Plugin Library.
- Package option artifact identity.
- CPS component normalization.
- Adapter sidecar isolation from graph identity.

### End-to-end examples

- Plain Executable build/run without NGIN.Core.
- Hosted Executable explicitly linking NGIN.Core.
- Reflection generation without duplicate package/action selection.
- Windows-only accessibility package through `<When>`.
- Test Executable through `ngin test`.
- Benchmark Executable through `ngin benchmark`.
- Plugin Library staging with application-owned registration.
- CPS package consumption.
- Package publish with generated CPS.

## Risks and mitigations

### Executable is less product-oriented than Application

This is intentional. Application describes purpose, while Executable describes the artifact NGIN builds. Run, Stage, and Publish express application behavior without inventing a separate artifact kind.

### Tool identity moves out of the project root

This is also intentional. Tool is a consumer-facing package role. One Executable may be used both manually and as a host Tool. Package metadata is the correct owner of that distinction.

### Test and Benchmark look less prominent

They remain first-class registrations, CLI commands, plan types, graph nodes, and editor concepts. They simply stop pretending to be different compiled artifacts.

### Plugin naming differs from CMake MODULE

`Plugin` avoids collision with C++ modules and NGIN.Core modules. Backend adapters map it to CMake `MODULE` and CPS module components.

### Friendly syntax hides normalized facts

Manifest IR, effective inspection, provenance, and explain are release requirements.

### Capability provider selection becomes surprising

Ambiguity is an error. Workspace preferences are explicit and inspectable. Authored order never chooses a winner.

### CPS cannot describe NGIN actions or runtime activation

`.nginpkg` overlays handle actions, capabilities, assets, and staging. Runtime activation remains outside both CPS and build manifests.

### Built-in defaults feel magical

Keep them small, stable, named, and visible with built-in provenance.

### One breaking cutover creates a large diff

Introduce Manifest IR first, migrate canonical examples in dependency order, and compare graphs before deleting the old grammar.

## Acceptance criteria

The Next authored model is successful when:

1. A C++ engineer immediately recognizes Executable and Library as the two build product kinds.
2. Application, Tool, Test, Benchmark, Plugin, C++ module, and NGIN.Core module no longer compete in one type list.
3. A minimal Executable is understandable without schema documentation.
4. Reflection generation is expressed once.
5. A Windows-only package addition requires one shallow `<When>` block.
6. A project can require an abstract capability.
7. A runnable Executable needs no explicit Run declaration.
8. A Test is an Executable registration and can still be run directly for debugging.
9. A Benchmark is an Executable registration with distinct runner semantics.
10. A Tool is an Executable exposed through package metadata and resolved in host context.
11. A Plugin is a loadable Library artifact and package export, never automatic runtime registration.
12. An ordinary NGIN product does not need a parallel handwritten package manifest.
13. A workspace does not enumerate every local package or redefine conventional host configurations.
14. CPS-described components can be consumed without duplicating their compiled metadata.
15. NGIN.Core remains optional and runtime lifecycle is application-owned.
16. Every inferred fact can be inspected and explained with provenance.
17. Equivalent inputs produce byte-identical canonical Composition Graphs.

## Final architectural rule

Every authored declaration must answer one of these questions:

- What artifact am I producing: Executable or Library?
- What does it use?
- What is compiled?
- What must be generated?
- How may an Executable be run, tested, or benchmarked?
- What artifact must be staged?
- What is published?
- Under which typed selection does an addition apply?

If a declaration answers “how does the resolver internally represent this?”, it belongs in Manifest IR or the Composition Graph.

If it answers “how is this artifact consumed from a package?”, it belongs in package export metadata such as Tool or Plugin.

If it answers “what services or runtime modules does the application register and run?”, it belongs in application code or NGIN.Core—not in `.ngin*` authoring.

## Reference model alignment

- [CMake `add_executable`](https://cmake.org/cmake/help/latest/command/add_executable.html)
- [CMake `add_library`](https://cmake.org/cmake/help/latest/command/add_library.html)
- [CMake `add_test`](https://cmake.org/cmake/help/latest/command/add_test.html)
- [Meson build targets](https://mesonbuild.com/Build-targets.html)
- [Meson `test()`](https://mesonbuild.com/Reference-manual_functions_test.html)
- [Meson `benchmark()`](https://mesonbuild.com/Reference-manual_functions_benchmark.html)
- [Common Package Specification](https://cps-org.github.io/cps/overview.html)
