# NGIN Project Model VNext Freeform Plan

Status: Proposed

## Summary

This plan collects a broad, compatibility-breaking redesign wishlist for the
NGIN project model. It is intentionally unconstrained by V2 compatibility and
should be treated as a strategic design backlog, not an approved implementation
contract.

The main direction is to keep `.nginproj` as a declarative project manifest
rather than turning it into a general build engine, while borrowing the best
parts of `.csproj` and MSBuild authoring:

- concise defaults for common projects
- reusable imports and SDK-style project templates
- profile inheritance instead of repeated configuration blocks
- a generalized item model with metadata
- package-driven extension points
- stronger inspectability and migration tooling

The highest-value changes are:

- add profile inheritance and project/workspace defaults
- add imports and SDK-style project declarations
- generalize source, config, content, and generated files into typed items
- make packages the primary extension mechanism
- add controlled lifecycle hooks without exposing arbitrary build scripting as
  the core model

## Goals

- Make common project files shorter without hiding the resolved model.
- Make large project files easier to factor, reuse, and inspect.
- Reduce repeated platform, architecture, environment, launch, and build
  settings.
- Preserve NGIN's domain-specific manifest strengths: validation, graphing,
  staging, runtime composition, and package-aware tooling.
- Add controlled extensibility points for code generation, assets, staging, and
  backend-specific customization.
- Make packages responsible for reusable behavior instead of requiring every
  app project to restate low-level details.
- Improve developer ergonomics for users who already know `.csproj`-style
  project files.
- Keep CMake as a backend rather than replacing it with a custom general build
  engine.

## Non-Goals

- Do not clone MSBuild.
- Do not make arbitrary script execution the foundation of the project model.
- Do not require every project to use NGIN.Core.
- Do not move generated backend files into authored project state.
- Do not make package side effects implicit or invisible.
- Do not make compatibility with V2 the deciding factor for this plan.
- Do not implement every idea in one schema revision.

## Design Principles

### Declarative First

Project files should describe intended project shape and composition. Commands
such as `ngin graph`, `ngin explain`, and `ngin validate` should be able to
reason about the manifest before backend generation runs.

### Small Common Case

A minimal app should stay compact. Most common values should be inferred from
project defaults, workspace defaults, selected SDK, or conventions.

### Explicit Escape Hatches

Escape hatches should exist, but they should be structured. The preferred shape
is "declare inputs, outputs, command, environment, and lifecycle point" rather
than opaque script blobs.

### Package-Oriented Extensibility

Reusable behavior should come from packages. Projects should opt into package
features and contributions explicitly.

### Inspectable Resolution

Every inherited default, imported item, selected package feature, generated
source, copied asset, runtime module, and launch decision should be explainable
by tooling.

## Proposed Schema Direction

Introduce a breaking VNext schema, likely `SchemaVersion="3"`, with cleaner
names and stronger reuse features.

Example direction:

```xml
<Project SchemaVersion="3"
         Sdk="NGIN.Application/3"
         Name="App.NativeMinimal">
  <Sources>
    <Private>
      <Root Path="src" />
    </Private>
  </Sources>
</Project>
```

The SDK, workspace defaults, and inferred output would provide the usual
application defaults. The expanded normalized model would still contain output,
build backend, language, profile, launch, and staging information.

## Naming Changes

### Rename Configuration To Profile

The current model has project configurations and backend build configurations.
That is technically clear but mentally noisy for C++ and CMake users.

Proposed change:

- `Configurations` becomes `Profiles`
- `Configuration` becomes `Profile`
- CLI `--configuration` can eventually become `--profile`
- generated launch metadata can still record the selected profile

Example:

```xml
<Profiles>
  <Profile Name="Runtime" BuildType="Debug" Platform="linux-x64" />
  <Profile Name="Shipping" Extends="Runtime" BuildType="Release" />
</Profiles>
```

### Rename BuildConfiguration To BuildType

`BuildType` better matches CMake's single-config terminology and avoids
confusion with project profile selection.

Example:

```xml
<Profile Name="Runtime" BuildType="Debug" />
```

### Replace OperatingSystem And Architecture Repetition With Platform

Define platforms once and reference them by name.

```xml
<Platforms>
  <Platform Name="linux-x64"
            OperatingSystem="linux"
            Architecture="x64" />
  <Platform Name="windows-x64"
            OperatingSystem="windows"
            Architecture="x64" />
</Platforms>
```

Profiles then select:

```xml
<Profile Name="Runtime" Platform="linux-x64" />
```

Typed selectors can still match either `Platform`, `OperatingSystem`, or
`Architecture` depending on the needed precision.

## Defaults And Inference

### Project Defaults

Add a root-level defaults surface. The goal is to remove repeated values from
every profile.

```xml
<Defaults BuildType="Debug"
          Platform="linux-x64"
          Environment="local"
          Language="CXX"
          LanguageStandard="23" />
```

Possible defaults:

- `BuildType`
- `Platform`
- `Environment`
- `Language`
- `LanguageStandard`
- `Backend`
- `BuildMode`
- launch executable
- launch working directory
- output name
- output target
- warning policy

### Workspace Defaults

Workspace-level defaults should cover values repeated across many projects.

Possible workspace defaults:

- standard platforms
- default build type
- language standard
- warning policy
- package feeds
- central package versions
- common imports
- common project SDK version

### Inferred Output

`Output` should be optional for the common case. The project type and name can
infer output kind, artifact name, and backend target.

Example:

```xml
<Project SchemaVersion="3"
         Sdk="NGIN.Application/3"
         Name="App.NativeMinimal" />
```

Could normalize to:

```xml
<Output Kind="Executable"
        Name="App.NativeMinimal"
        Target="App.NativeMinimal" />
```

Explicit output remains available when names differ.

### Project-Level Launch Defaults

Most application profiles repeat the same launch metadata. Move launch defaults
to the project root and allow profile overrides.

```xml
<Launch Executable="$(OutputName)" WorkingDirectory="." />
```

Profile-specific override:

```xml
<Profile Name="Service">
  <Launch Arguments="--service" />
</Profile>
```

## Profile Model

### Profile Inheritance

Profiles should be able to inherit from another profile and override only the
differences.

```xml
<Profiles>
  <Profile Name="Runtime"
           BuildType="Debug"
           Platform="linux-x64"
           Environment="development" />

  <Profile Name="Diagnostics"
           Extends="Runtime">
    <Use Feature="Diagnostics" />
  </Profile>

  <Profile Name="Shipping"
           Extends="Runtime"
           BuildType="Release"
           Environment="production" />
</Profiles>
```

Inheritance rules should be explicit:

- scalar attributes override parent values
- item collections append by default
- removals use explicit remove syntax
- conflicts are diagnostics, not silent behavior
- cycles are validation errors

### Reusable Profile Definitions

Allow reusable profile templates at project, workspace, or imported file scope.

Examples:

- `Runtime`
- `Diagnostics`
- `Shipping`
- `Editor`
- `Server`
- `Service`
- `Reflection`

```xml
<ProfileTemplates>
  <ProfileTemplate Name="Diagnostics">
    <Use Feature="Diagnostics" />
    <Define Value="NGIN_DIAGNOSTICS" />
  </ProfileTemplate>
</ProfileTemplates>
```

Project usage:

```xml
<Profile Name="Runtime.Diagnostics"
         Extends="Runtime"
         Template="Diagnostics" />
```

### Profile Axes

Consider making important profile axes visible as separate concepts:

- `BuildType`
- `Platform`
- `Environment`
- `RuntimeMode`
- `FeatureSet`

The selected profile resolves those axes into one final selection context.

## Imports And SDK-Style Projects

### Imports

Add import support for shared project fragments.

```xml
<Import Project="../build/common.nginprops" />
```

Possible imported content:

- defaults
- platforms
- conditions
- item groups
- build settings
- package versions
- profile templates
- package feature aliases

Rules:

- imports are resolved relative to the importing file
- import order is deterministic
- duplicate definitions are either explicit overrides or validation errors
- imported values preserve provenance for `ngin explain`
- import cycles are validation errors

### SDK-Style Projects

Add SDK-style project declarations for common project kinds.

```xml
<Project SchemaVersion="3"
         Sdk="NGIN.Application/3"
         Name="App.Basic" />
```

Candidate SDKs:

- `NGIN.Application`
- `NGIN.Tool`
- `NGIN.Library`
- `NGIN.HostedApplication`
- `NGIN.PluginHost`
- `NGIN.Service`
- `NGIN.GameClient`
- `NGIN.GameServer`

SDKs can provide:

- default output kind
- default build backend
- default language settings
- launch defaults
- staging behavior
- common package references
- default runtime model behavior

SDKs must remain inspectable. They should behave like explicit imports with
known semantics, not invisible magic.

## Generalized Item Model

### Motivation

The current model has separate concepts for sources, config sources, contents,
and build settings. That is readable, but it can produce schema growth when new
file-like things appear.

Introduce an internal item model similar in spirit to MSBuild items, but keep
domain-specific aliases for author readability.

### Item Kinds

Potential built-in item kinds:

- `Compile`
- `Header`
- `PublicHeader`
- `PrivateHeader`
- `SourceRoot`
- `Config`
- `Content`
- `RuntimeAsset`
- `GeneratedSource`
- `ReflectionInput`
- `ToolInput`
- `PackageAsset`
- `StageFile`

### Domain-Specific Aliases

Keep readable surfaces where they make sense:

```xml
<Sources>
  <Private>
    <Root Path="src" />
  </Private>
</Sources>
```

Normalize internally to item declarations.

Generic form:

```xml
<Items>
  <Item Kind="Compile" Include="src/**/*.cpp" />
  <Item Kind="Config"
        Include="config/app.cfg"
        TargetPath="config/app.cfg" />
</Items>
```

### Item Metadata

Support metadata on items:

- `Kind`
- `Include`
- `Exclude`
- `Path`
- `TargetPath`
- `CopyToOutput`
- `Visibility`
- `Generator`
- `Condition`
- `Platform`
- `BuildType`
- `Environment`
- `Package`
- `Stage`

Example:

```xml
<Item Kind="Content"
      Include="assets/**/*.png"
      TargetPath="assets/%(RecursiveDir)%(Filename)%(Extension)"
      CopyToOutput="PreserveNewest" />
```

### Item Groups

Item groups should carry inherited selectors and metadata.

```xml
<ItemGroup Platform="linux-x64">
  <Item Kind="Compile" Include="src/platform/linux/**/*.cpp" />
  <Item Kind="Content" Include="assets/linux/**" />
</ItemGroup>
```

Group metadata applies to child items as implicit AND or inherited metadata.

### Item Removal And Update

Large imported/default item sets need explicit mutation operations.

Potential operations:

```xml
<Item Kind="Compile" Remove="src/experimental/**" />
<Item Kind="Content" Update="assets/**/*.png" CopyToOutput="Always" />
```

Rules should be strict and inspectable. Removing or updating nothing should be a
warning in strict validation mode.

### Remove Legacy SourceRoots

In a breaking schema, remove `SourceRoots`. `Sources` and the normalized item
model replace it.

## Conditions And Selection

### Keep Typed Conditions

The structured condition model is one of NGIN's strongest current design
choices. Keep typed selectors and named conditions rather than making free-form
string expressions the default.

### Rename When To Condition

Consider replacing `When="..."` with `Condition="..."` for clearer
cross-tooling familiarity.

Current:

```xml
<Definition Value="APP_DEBUG" When="LocalDebug" />
```

Proposed:

```xml
<Definition Value="APP_DEBUG" Condition="LocalDebug" />
```

Compatibility aliases could exist during migration, but VNext can choose one
canonical spelling.

### Predefined Conditions

Provide standard conditions from the selected context:

- `Debug`
- `Release`
- `RelWithDebInfo`
- `MinSizeRel`
- `Desktop`
- `Windows`
- `Linux`
- `MacOS`
- `Local`
- `Development`
- `Production`

These should be normal named conditions in the normalized model so tooling can
explain them.

### Condition Scopes

Allow conditions at multiple scopes:

- workspace
- imported file
- SDK
- package
- project

Resolution should define shadowing and override rules. Prefer explicit
qualification or validation errors over surprising shadowing.

### Condition Diagnostics

Add diagnostics that explain inclusion and exclusion.

Examples:

```text
ngin explain item src/platform/linux/window.cpp --profile Runtime
ngin explain condition LocalDebug --profile Runtime
```

The output should show:

- selected profile context
- condition source file
- inherited group conditions
- final match result
- why each branch matched or failed

## Build Model

### Keep CMake As Backend

NGIN should continue generating backend input and orchestrating workflow. It
should not become a general-purpose replacement for CMake.

### Backend Settings

Add structured backend settings for common CMake needs.

Possible CMake-specific metadata:

- cache variables
- target properties
- presets
- generator preference
- toolchain file
- package options
- exported compile commands

Example:

```xml
<Backend Name="CMake">
  <CacheVariables>
    <Variable Name="CMAKE_POSITION_INDEPENDENT_CODE" Value="ON" />
  </CacheVariables>
  <TargetProperties>
    <Property Name="CXX_VISIBILITY_PRESET" Value="hidden" />
  </TargetProperties>
</Backend>
```

### Controlled Lifecycle Hooks

Add lifecycle hooks, but keep them constrained.

Possible hooks:

- `BeforeRestore`
- `AfterRestore`
- `BeforeConfigure`
- `AfterConfigure`
- `BeforeGenerate`
- `AfterGenerate`
- `BeforeBuild`
- `AfterBuild`
- `BeforeStage`
- `AfterStage`
- `BeforeRun`

Project usage should usually enable package-provided hooks:

```xml
<Use Feature="ShaderCompilation" />
```

Direct custom command escape hatch:

```xml
<Hook Name="BeforeStage">
  <Command Name="CompileShaders"
           Tool="shaderc"
           Inputs="shaders/**/*.hlsl"
           Outputs="$(IntermediateDir)/shaders/*.spv"
           WorkingDirectory="$(ProjectDir)" />
</Hook>
```

Required fields:

- declared inputs
- declared outputs
- command/tool
- working directory
- environment
- lifecycle point

This keeps incremental behavior and graph inspection possible.

### Code Generation Contributions

Generalize MetaGen as one generator in a larger generator model.

```xml
<Generators>
  <Generator Name="MetaGen"
             Enabled="true"
             Inputs="include/**/*.hpp"
             Output="$(GeneratedDir)/reflection.cpp" />
</Generators>
```

Packages should be able to contribute generators:

```xml
<Use Feature="Reflection" />
```

The project should still be able to inspect exactly what files are generated
and where they enter the build.

### Asset Pipeline Contributions

Add first-class asset processing for game/tool/app workflows.

Possible asset operations:

- copy
- transform
- compress
- compile
- package
- fingerprint
- stage

Example:

```xml
<Assets>
  <Asset Include="assets/textures/**/*.png"
         Processor="TextureCompiler"
         TargetPath="assets/textures" />
</Assets>
```

Asset processors should normally come from packages.

## Package Model

### Packages As Primary Extension Mechanism

Packages should declare reusable identity, artifacts, content, runtime
participants, code generators, asset processors, hooks, defaults, and features.

Projects should opt into those contributions explicitly.

### Package Capabilities

Add a carefully designed capability or feature surface.

```xml
<Capabilities>
  <Capability Name="Diagnostics">
    <Requires Package="NGIN.Diagnostics" />
    <EnablesPlugin Name="NGIN.Diagnostics" />
    <Defines Value="NGIN_DIAGNOSTICS" />
  </Capability>
</Capabilities>
```

Project usage:

```xml
<Use Feature="Diagnostics" />
```

This makes the common path shorter while preserving the distinction between a
package dependency and enabled runtime behavior.

### Package Features

Package features should support:

- package references
- artifact references
- compile definitions
- include directories
- runtime modules
- plugins
- content files
- generators
- hooks
- environment variables
- config files

Features should be explicit and inspectable.

### Package Defaults

Packages may declare default contributions, but project policy decides whether
defaults auto-apply.

Possible policy:

```xml
<PackagePolicy DefaultFeatures="Explicit" />
```

Modes:

- `Explicit`: nothing applies unless requested
- `SafeDefaults`: non-invasive defaults apply
- `PackageDefault`: package decides

### Central Package Versions

Move common package versions to workspace or central package metadata.

```xml
<PackageVersions>
  <Package Name="NGIN.Core" Version=">=0.1.0 &lt;0.2.0" />
  <Package Name="NGIN.Diagnostics" Version=">=0.1.0 &lt;0.2.0" />
</PackageVersions>
```

Project reference can then be shorter:

```xml
<Package Name="NGIN.Core" />
```

### Package Lock Files

Add lock files for reproducible package resolution.

Potential lock file goals:

- exact package versions
- source/feed identity
- content hash
- resolved dependency graph
- toolchain package versions

### Version Resolution Policy

Make version resolution a clear workspace concept rather than scattered
behavior.

Possible policy controls:

- allow floating ranges
- lock restore by default
- fail on ambiguous versions
- central version override rules
- transitive dependency conflict strategy

## Runtime Model

### Reduce Runtime Verbosity In App Projects

Application projects should not need to restate every low-level runtime module
and plugin detail. Packages and app modules should carry most of the reusable
runtime graph.

### Short App Module Syntax

Add a concise app-module declaration for common application-owned modules.

```xml
<Runtime>
  <Module Name="App.Basic.Runtime" Stage="Features" />
</Runtime>
```

Expanded metadata can still exist when needed.

### Runtime Presets

SDKs or runtime presets should cover common host shapes.

Candidates:

- `HostedApplication`
- `PluginHost`
- `Service`
- `Tool`
- `GameClient`
- `GameServer`

Example:

```xml
<Project Sdk="NGIN.HostedApplication/3" Name="App.Basic" />
```

This could imply the right NGIN.Core feature, basic bootstrap behavior, and
launch conventions.

### Profile-Aware Module Enablement

Runtime enablement should compose with profiles and features.

```xml
<Profile Name="Runtime.Diagnostics" Extends="Runtime">
  <Use Feature="Diagnostics" />
</Profile>
```

The feature can enable the package, plugin, module, config, and definitions
needed for diagnostics.

### Runtime And Build Boundary

Keep runtime metadata visibly separate from build metadata. Runtime module
enablement should not silently imply build dependencies unless represented
through package features or explicit references.

## CLI And Tooling

### ngin explain

Add a general explanation command.

```text
ngin explain --project App.nginproj --profile Runtime
```

It should explain:

- selected profile
- inherited defaults
- imports and SDKs
- selected platform
- selected environment
- package references
- package features
- source items
- generated items
- config and content staging
- runtime modules and plugins
- launch metadata
- backend settings

### Item Explanation

Add item-level diagnostics.

```text
ngin explain item src/main.cpp --project App.nginproj --profile Runtime
```

It should show:

- item kind
- declaring file
- selectors and conditions
- inherited metadata
- include/exclude pattern match
- final target path or build role

### Condition Explanation

Add condition-level diagnostics.

```text
ngin explain condition Desktop --profile Runtime
```

It should show:

- condition definition source
- expanded condition tree
- selected profile context
- match result

### Formatting

Add schema-aware formatting.

```text
ngin format --project App.nginproj
```

Formatting should preserve comments where practical and enforce canonical
attribute ordering.

### Migration

Add migration tooling.

```text
ngin migrate --project App.nginproj --to-schema 3
```

Migration should:

- rename `Configurations` to `Profiles`
- rename `BuildConfiguration` to `BuildType`
- convert repeated OS/architecture to platforms
- convert `ConfigSources` and `Contents` to items where appropriate
- move repeated launch metadata to root defaults
- preserve old behavior in normalized output
- produce a report of manual follow-up work

### Project Templates

Add first-class project creation commands.

```text
ngin new app
ngin new lib
ngin new tool
ngin new hosted-app
ngin new service
ngin new game-client
ngin new game-server
```

Templates should use SDK-style projects and concise defaults.

### Validation Levels

Add validation strictness levels.

```text
ngin validate --level basic
ngin validate --level strict
ngin validate --level release
```

Possible strict diagnostics:

- unused conditions
- unused imports
- unused profile templates
- unused package references
- unused environment variables
- removed item pattern matched nothing
- update item pattern matched nothing
- duplicate item target paths
- launch executable mismatch
- package feature enabled but no contribution selected

### Graph Output Formats

Add graph output formats:

- text
- JSON
- DOT
- Mermaid

Example:

```text
ngin graph --format json
ngin graph --format mermaid
```

## Schema And Editor Support

### Canonical Normalized Model

Define a canonical model independent of XML syntax. XML is the authored
serialization; the normalized model is what validation, graphing, backend
generation, and editor tools consume.

### Schema Files

Publish machine-readable schemas for editor completion and validation.

Potential files:

- `schemas/nginproj-v3.xsd`
- `schemas/nginpkg-v3.xsd`
- `schemas/ngin-workspace-v3.xsd`

### Case Policy

Make case sensitivity rules explicit for:

- project names
- profile names
- condition names
- platform names
- environment names
- package names
- feature names
- item kinds

Prefer case-preserving names with validation against ambiguous case-only
duplicates.

## Example: Minimal Application

Authored:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3"
         Sdk="NGIN.Application/3"
         Name="App.NativeMinimal">
  <Sources>
    <Private>
      <Root Path="src" />
    </Private>
  </Sources>
</Project>
```

Normalized concepts:

- project type: application
- output kind: executable
- output name: `App.NativeMinimal`
- backend: CMake
- build mode: generated
- language: CXX
- language standard: workspace or SDK default
- default profile: `Runtime`
- default platform: workspace default
- default environment: workspace or SDK default
- launch executable: output executable

## Example: Diagnostics Profile

```xml
<Project SchemaVersion="3"
         Sdk="NGIN.HostedApplication/3"
         Name="App.Basic">
  <Sources>
    <Private>
      <Root Path="src" />
    </Private>
  </Sources>

  <Use Package="NGIN.Core" />

  <Profiles>
    <Profile Name="Runtime" />

    <Profile Name="Diagnostics" Extends="Runtime">
      <Use Feature="Diagnostics" />
    </Profile>
  </Profiles>
</Project>
```

The diagnostics feature can expand to:

- package reference to `NGIN.Diagnostics`
- plugin enablement
- optional compile definition
- optional config source
- optional runtime module enablement

## Example: Shared Workspace Defaults

```xml
<Workspace SchemaVersion="3" Name="Examples">
  <Defaults Platform="linux-x64"
            BuildType="Debug"
            LanguageStandard="23" />

  <Platforms>
    <Platform Name="linux-x64"
              OperatingSystem="linux"
              Architecture="x64" />
  </Platforms>

  <PackageVersions>
    <Package Name="NGIN.Core" Version=">=0.1.0 &lt;0.2.0" />
    <Package Name="NGIN.Diagnostics" Version=">=0.1.0 &lt;0.2.0" />
  </PackageVersions>
</Workspace>
```

## Implementation Phases

### Phase 1: Normalized Model Spike

- Define an internal normalized project model separate from V2 XML structs.
- Add provenance tracking for values.
- Add a read-only normalizer for current V2 manifests.
- Add JSON dump support for normalized manifests.

### Phase 2: Profiles And Defaults

- Add VNext `Profiles`.
- Add `BuildType`.
- Add root defaults.
- Add platform definitions.
- Add project-level launch defaults.
- Implement profile inheritance.
- Add diagnostics for inheritance cycles and ambiguous overrides.

### Phase 3: Imports And SDKs

- Add import file format.
- Add import resolution and cycle detection.
- Add SDK declaration support.
- Represent SDKs as known imports with provenance.
- Add workspace defaults.

### Phase 4: Item Model

- Add normalized item model.
- Normalize `Sources`, `ConfigSources`, and `Contents` to items.
- Add item metadata.
- Add item group metadata inheritance.
- Add item remove/update operations.
- Add item explanation diagnostics.

### Phase 5: Package Features

- Add package capabilities or features.
- Add explicit feature opt-in from projects and profiles.
- Add central package versions.
- Add package feature explanation output.
- Add package lock file design.

### Phase 6: Build Extensions

- Add controlled lifecycle hook model.
- Add custom command schema with inputs and outputs.
- Generalize MetaGen under generator contributions.
- Add package-provided generators.
- Add package-provided asset processors.

### Phase 7: Runtime Simplification

- Add runtime presets through SDKs or features.
- Add concise module syntax.
- Move common NGIN.Core wiring into package features or SDK defaults.
- Add profile-aware runtime feature selection.

### Phase 8: Tooling

- Add `ngin explain`.
- Add `ngin format`.
- Add `ngin migrate --to-schema 3`.
- Add validation levels.
- Add graph output formats.
- Publish schema files for editor tooling.

## Priorities

The first three changes should be:

1. Profile inheritance and defaults.
2. Imports and SDK-style projects.
3. Generalized item model.

These have the best ratio of authoring improvement to conceptual risk. They
make `.nginproj` feel more powerful and less repetitive without immediately
opening the door to arbitrary build scripting.

The next tier should be:

1. Package features and central versions.
2. `ngin explain`.
3. Migration tooling.

Build hooks and asset/codegen extensibility should come after the normalized
model, provenance, and explanation tooling exist. Otherwise the extension model
will be hard to debug.

## Open Questions

- Should `Profile` fully replace `Configuration`, or should both terms remain
  visible in different contexts?
- Should SDKs live as package assets, built-in CLI metadata, or workspace
  imports?
- Should central package versions live in `.ngin`, a separate
  `packages.nginversions`, or both?
- Should item metadata use XML attributes only, child metadata elements, or
  both?
- How much package default behavior is acceptable before package use becomes
  surprising?
- Should direct custom commands be allowed in projects, or only through
  packages?
- Should `Condition="..."` accept only named conditions, or eventually support
  structured inline expressions?
- Should XML remain the only authored syntax, or should VNext define the model
  independently enough to allow another syntax later?

## Compatibility Position

This plan intentionally ignores compatibility as a design constraint. If any of
these ideas become active work, they should be split into smaller plans and
converted into specs with migration rules.

The expected migration path from V2 to VNext would require:

- automated manifest migration
- compatibility aliases for CLI options
- normalized-model comparison tests
- side-by-side examples
- deprecation documentation
- strict diagnostics for behavior that cannot migrate automatically
