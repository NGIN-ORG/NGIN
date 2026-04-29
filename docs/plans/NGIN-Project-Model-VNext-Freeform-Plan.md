# NGIN Project Model VNext Freeform Plan

Status: V3 Foundation, Phase A Model Factoring, And Phase B Unified Inputs Implemented; Strategic Backlog Remains

## Implementation Progress

Implemented so far:

- V3 `.nginproj` parsing in the CLI, normalized into the existing internal
  build model.
- V3 `.nginproj` parsing in `NGIN.Core` for hosted-runtime project loading.
- `DefaultProfile`, `Profiles`, `Profile`, `BuildType`, `Platform`,
  `Condition`, `Template`, and inferred `Output` support.
- Ordered profile inheritance through `Profile Extends="..."`.
- Typed root/profile/environment/template/package input blocks under
  `<Inputs>`.
- `--profile` CLI selection.
- V3 `.nginlaunch` emission with `Profile`, `BuildType`, `Platform`, and
  config metadata under `<Inputs>`.
- Removal of project-model compatibility aliases for authored V2 vocabulary in
  the CLI, NGIN.Core project parser, and VS Code manifest parser.
- Repository-authored `.nginproj` examples migrated to V3 vocabulary.
- VS Code manifest parsing, project authoring helpers, snippets, task/debug
  schema, status UI model, and unit tests updated for profile/input vocabulary.
- Public NGIN.Core project APIs moved from configuration terminology to profile
  terminology (`ProfileDefinition`, `SetProfile`, `GetProfileName`,
  `ProfileName`).
- CMake test harnesses and repository smoke paths moved from `CONFIGURATION`
  variables to `PROFILE` variables where they mean NGIN project profiles.
- Legacy authored aliases and compatibility parser paths removed from active
  CLI, NGIN.Core, and VS Code project/launch handling.
- Phase A model factoring implemented across CLI, NGIN.Core, VS Code, examples,
  docs, and tests:
  - `.nginmodel` shared model files
  - workspace/project includes with missing-file and cycle diagnostics
  - workspace and project defaults
  - root project launch defaults with `$(OutputName)`
  - built-in and authored project templates
  - reusable profile templates
  - V3 workspace manifests
- Phase B unified input model implemented across CLI, NGIN.Core, VS Code,
  examples, packages, docs, and tests:
  - typed input blocks: `Sources`, `Headers`, `Configs`, `Contents`, `Assets`,
    `Generated`, and `ToolInputs`
  - structured `File`, `Directory`, and `Glob` entries plus simple text file
    lists
  - `Remove`, `Override`, selectors, scan filters, staging roots, and input
    metadata parsing
  - active `Source`, `Config`, `Content`, `Asset`, `Generated`, and `ToolInput`
    normalized kinds
  - generated CMake and MetaGen source discovery from `Source`/`Generated`
    inputs
  - staging for `Config`, `Content`, `Asset`, and targeted `Generated` inputs
  - package `SchemaVersion="3"` inputs replacing authored package `Contents`
  - launch metadata emission using normalized `<Input>` entries
  - old authored generic `Input`/`InputSet`, `Form`, `SourceRoots`,
    `Inputs/Config`, and top-level `Contents` rejected by active parsers
- Active project, CLI, launch-manifest, and authoring docs moved toward V3.
- Verification completed against workspace tests, NGIN.Core tests, VS Code unit
  tests, NGIN.Core BasicHost example build, repository stale-vocabulary scans,
  and the App.NativeMinimal/App.HostedCore/App.Basic smoke paths.

Still remaining from the broader plan:

- Dependency version policy and package feature/capability expansion.
- Pipeline phase contributions.
- `ngin explain`, `ngin create`, format, one-way offline migration, and
  schema-file tooling.

## Summary

This plan collects a broad, compatibility-breaking redesign wishlist for the
NGIN project model. It is intentionally unconstrained by V2 compatibility and
should be treated as a strategic design backlog, not an approved implementation
contract.

The main direction is to keep `.nginproj` as a declarative project manifest
rather than turning it into a general build engine, while borrowing broad
authoring lessons from mature project-file ecosystems without copying their
public vocabulary:

- concise defaults for common projects
- reusable shared model files and project templates
- profile inheritance instead of repeated configuration blocks
- a generalized project input model with metadata
- package-driven extension points
- stronger inspectability and migration tooling

The highest-value changes are:

- add profile inheritance and project/workspace defaults
- add shared model includes and template-based project declarations
- generalize source, config, content, and generated files into typed inputs
- make packages the primary extension mechanism
- add controlled pipeline phases without exposing arbitrary build scripting as
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
project defaults, workspace defaults, selected template, or conventions.

### Explicit Escape Hatches

Escape hatches should exist, but they should be structured. The preferred shape
is "declare inputs, outputs, command, environment, phase, and timing point" rather
than opaque script blobs.

### Package-Oriented Extensibility

Reusable behavior should come from packages. Projects should opt into package
features and contributions explicitly.

### Inspectable Resolution

Every inherited default, included input, selected package feature, generated
source, copied asset, runtime module, and launch decision should be explainable
by tooling.

## Proposed Schema Direction

Introduce a breaking VNext schema, likely `SchemaVersion="3"`, with cleaner
names and stronger reuse features.

Example direction:

```xml
<Project SchemaVersion="3"
         Template="Application"
         Name="App.NativeMinimal">
  <Inputs>
    <Sources Path="src" />
  </Inputs>
</Project>
```

The template, workspace defaults, and inferred output would provide the usual
application defaults. The expanded normalized model would still contain output,
build backend, language, profile, launch, and staging information.

## Naming Changes

### Rename Configuration To Profile

The current model has project configurations and backend build configurations.
That is technically clear but mentally noisy for C++ and CMake users.

Proposed change:

- `Configurations` becomes `Profiles`
- `Configuration` becomes `Profile`
- CLI selection uses `--profile`
- generated launch metadata can still record the selected profile

Example:

```xml
<Profiles>
  <Profile Name="Runtime" BuildType="Debug" Platform="linux-x64" />
  <Profile Name="Shipping" Extends="Runtime" BuildType="Release" />
</Profiles>
```

### Build Type Vocabulary

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
- dependency version policy
- common shared model files
- common project template version

### Inferred Output

`Output` should be optional for the common case. The project type and name can
infer output kind, artifact name, and backend target.

Example:

```xml
<Project SchemaVersion="3"
         Template="Application"
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
- input collections append by default
- removals use explicit remove syntax
- conflicts are diagnostics, not silent behavior
- cycles are validation errors

### Reusable Profile Definitions

Allow reusable profile templates at project, workspace, or shared model scope.

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

## Shared Models And Project Templates

### Shared Model Includes

Add include support for shared project fragments without using MSBuild-style
`.props` terminology.

```xml
<Include Model="../build/common.nginpart" />
```

Possible included content:

- defaults
- platforms
- conditions
- input sets
- build settings
- package versions
- profile templates
- package feature aliases

Rules:

- includes are resolved relative to the including file
- include order is deterministic
- duplicate definitions are either explicit overrides or validation errors
- included values preserve provenance for `ngin explain`
- include cycles are validation errors

### Project Templates

Add template-based project declarations for common project kinds.

```xml
<Project SchemaVersion="3"
         Template="Application"
         Name="App.Basic" />
```

Candidate templates:

- `Application`
- `Tool`
- `Library`
- `HostedApplication`
- `PluginHost`
- `Service`
- `GameClient`
- `GameServer`

Templates can provide:

- default output kind
- default build backend
- default language settings
- launch defaults
- staging behavior
- common package references
- default runtime model behavior

Templates must remain inspectable. They should behave like explicit shared
model includes with known semantics, not invisible magic.

## Generalized Project Input Model

### Motivation

The current model has separate concepts for sources, config inputs, contents,
and build settings. That is readable, but it can produce schema growth when new
file-like things appear.

Introduce an internal project input model for files and generated artifacts, but
avoid public `Item` and `ItemGroup` names. The authored vocabulary should sound
like NGIN: sources, configs, assets, generated inputs, staging inputs, and
input sets.

### Input Kinds

Potential built-in input kinds:

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
<Inputs>
  <Sources Path="src" />
  <Configs>
    config/app.cfg
  </Configs>
</Inputs>
```

### Input Metadata

Support metadata on inputs:

- `Kind`
- `Path`
- `Pattern`
- `Exclude`
- `StagePath`
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
<Content Pattern="assets/**/*.png"
         StagePath="assets/{relativePath}"
         CopyToOutput="PreserveNewest" />
```

### Typed Input Blocks

Typed blocks carry inherited selectors and metadata.

```xml
<Sources Platform="linux-x64" Include="src/platform/linux/**/*.cpp" />
<Assets Platform="linux-x64">
  <Glob BasePath="assets" Include="linux/**" TargetRoot="assets" />
</Assets>
```

Block metadata applies to child entries as implicit AND or inherited metadata.

### Input Exclusion And Override

Large included/default input sets need explicit exclusion and override
operations. Avoid MSBuild-style `Update` semantics in the public vocabulary.

Potential operations:

```xml
<Exclude Kind="Compile" Pattern="src/experimental/**" />
<Override Kind="Content" Pattern="assets/**/*.png" CopyToOutput="Always" />
```

Rules should be strict and inspectable. Excluding or overriding nothing should
be a warning in strict validation mode.

### Remove Legacy SourceRoots

In a breaking schema, remove `SourceRoots`. `Sources` and the normalized input
model replace it.

## Conditions And Selection

### Keep Typed Conditions

The structured condition model is one of NGIN's strongest current design
choices. Keep typed selectors and named conditions rather than making free-form
string expressions the default.

### Rename When To Condition

Implemented in the V3 project model: selectable entries use
`Condition="..."` for clearer cross-tooling familiarity.

V2 form:

```xml
<Definition Value="APP_DEBUG" When="LocalDebug" />
```

V3 form:

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
- shared model file
- template
- package
- project

Resolution should define shadowing and override rules. Prefer explicit
qualification or validation errors over surprising shadowing.

### Condition Diagnostics

Add diagnostics that explain inclusion and exclusion.

Examples:

```text
ngin explain input src/platform/linux/window.cpp --profile Runtime
ngin explain condition LocalDebug --profile Runtime
```

The output should show:

- selected profile context
- condition source file
- inherited input-set conditions
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

### Controlled Pipeline Phases

Add pipeline phases, but keep them constrained.

Possible phases:

- `Restore`
- `Configure`
- `Generate`
- `Compile`
- `Stage`
- `Launch`

Contributions can run at a specific phase and timing point without using
MSBuild-style `Before...` and `After...` names.

Project usage should usually enable package-provided pipeline contributions:

```xml
<Use Feature="ShaderCompilation" />
```

Direct custom command escape hatch:

```xml
<Pipeline>
  <Step Name="CompileShaders"
        Phase="Stage"
        Timing="Before"
        Tool="shaderc"
        Inputs="shaders/**/*.hlsl"
        Outputs="$(IntermediateDir)/shaders/*.spv"
        WorkingDirectory="$(ProjectDir)" />
</Pipeline>
```

Required fields:

- declared inputs
- declared outputs
- command/tool
- working directory
- environment
- phase and timing point

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
  <Asset Pattern="assets/textures/**/*.png"
         Processor="TextureCompiler"
         StagePath="assets/textures" />
</Assets>
```

Asset processors should normally come from packages.

## Package Model

### Packages As Primary Extension Mechanism

Packages should declare reusable identity, artifacts, content, runtime
participants, code generators, asset processors, pipeline contributions,
defaults, and features.

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
- pipeline contributions
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

### Dependency Version Policy

Move common package versions to workspace dependency policy metadata. The goal
is central version management without adopting NuGet/.NET terminology.

```xml
<DependencyPolicy>
  <Versions>
    <Package Name="NGIN.Core" Version=">=0.1.0 &lt;0.2.0" />
    <Package Name="NGIN.Diagnostics" Version=">=0.1.0 &lt;0.2.0" />
  </Versions>
</DependencyPolicy>
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

### Runtime Templates

Project templates or runtime templates should cover common host shapes.

Candidates:

- `HostedApplication`
- `PluginHost`
- `Service`
- `Tool`
- `GameClient`
- `GameServer`

Example:

```xml
<Project Template="HostedApplication" Name="App.Basic" />
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
- shared model files and templates
- selected platform
- selected environment
- package references
- package features
- source inputs
- generated inputs
- config and content staging
- runtime modules and plugins
- launch metadata
- backend settings

### Input Explanation

Add input-level diagnostics.

```text
ngin explain input src/main.cpp --project App.nginproj --profile Runtime
```

It should show:

- input kind
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

### One-Way Offline Migration

Add explicit offline migration tooling for old repository snapshots. This
should not imply runtime parser compatibility in the active CLI, Core, or editor
tooling.

```text
ngin migrate --project App.nginproj --to-schema 3
```

Migration should:

- rename `Configurations` to `Profiles`
- use `BuildType` for backend build type
- convert repeated OS/architecture to platforms
- convert `ConfigInputs` and `Contents` to inputs where appropriate
- move repeated launch metadata to root defaults
- compare normalized output against the old tree before the compatibility code
  is removed from the migration tool itself
- produce a report of manual follow-up work

### Project Templates

Add first-class project creation commands.

```text
ngin create app
ngin create lib
ngin create tool
ngin create hosted-app
ngin create service
ngin create game-client
ngin create game-server
```

Templates should use template-based projects and concise defaults.

### Validation Levels

Add validation strictness levels.

```text
ngin validate --level basic
ngin validate --level strict
ngin validate --level release
```

Possible strict diagnostics:

- unused conditions
- unused shared model includes
- unused profile templates
- unused package references
- unused environment variables
- excluded input pattern matched nothing
- overridden input pattern matched nothing
- duplicate staged input paths
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
- input kinds

Prefer case-preserving names with validation against ambiguous case-only
duplicates.

## Example: Minimal Application

Authored:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3"
         Template="Application"
         Name="App.NativeMinimal">
  <Inputs>
    <Sources Path="src" />
  </Inputs>
</Project>
```

Normalized concepts:

- project type: application
- output kind: executable
- output name: `App.NativeMinimal`
- backend: CMake
- build mode: generated
- language: CXX
- language standard: workspace or template default
- default profile: `Runtime`
- default platform: workspace default
- default environment: workspace or template default
- launch executable: output executable

## Example: Diagnostics Profile

```xml
<Project SchemaVersion="3"
         Template="HostedApplication"
         Name="App.Basic">
  <Inputs>
    <Sources Path="src" />
  </Inputs>

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
- optional config input
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

  <DependencyPolicy>
    <Versions>
      <Package Name="NGIN.Core" Version=">=0.1.0 &lt;0.2.0" />
      <Package Name="NGIN.Diagnostics" Version=">=0.1.0 &lt;0.2.0" />
    </Versions>
  </DependencyPolicy>
</Workspace>
```

## Implementation Phases

The remaining work should be grouped by dependency order. Each phase should be
split into its own focused implementation plan before coding starts.

### Phase A: Model Factoring

Status: implemented.

Purpose: reduce repetition and make larger manifests manageable.

- Implemented shared model includes through `.nginmodel`.
- Implemented include resolution and cycle detection.
- Implemented workspace defaults.
- Implemented project defaults.
- Implemented project-level launch defaults.
- Implemented built-in and authored project templates.
- Finalized Phase A `Template="..."` semantics for `Application`, `Library`,
  `Tool`, and authored templates.
- Implemented reusable profile templates.
- Updated CLI, NGIN.Core, VS Code tooling, examples, specs, guide docs, and
  tests.

This phase should come first because later features need a clean way to share
and explain declarations.

### Phase B: Unified Input Model

Purpose: make sources, config, content, generated files, assets, and tool inputs
use one normalized model.

- Status: implemented.
- Added typed input blocks backed by the normalized input model.
- Normalized sources, headers, configs, package content, assets, generated
  files, and tool inputs.
- Added input metadata, typed entry inheritance, `Remove`, `Override`,
  `TargetRoot`, and `BasePath`.
- Removed active authored generic `Input`/`InputSet`, `Form`, `SourceRoots`,
  `Inputs/Config`, and top-level `Contents` compatibility paths.
- Preserved input provenance for launch metadata, staging diagnostics, and
  tooling.

This phase should land before generators and pipelines because those systems
need typed inputs and outputs.

### Phase C: Selection And Conditions

Purpose: make selection logic explicit, reusable, and explainable.

- Complete the `When` to `Condition` direction.
- Add predefined conditions.
- Define condition scopes.
- Apply conditions consistently to inputs, references, build settings, runtime
  entries, and package feature selections.
- Add diagnostics that explain inclusion and exclusion.
- Add condition explanation support.

This phase can overlap with the unified input model, but complex package and
pipeline behavior should wait until selection semantics are stable.

### Phase D: Package Extension Model

Purpose: make packages the primary reusable behavior unit.

- Add package capabilities.
- Add package features.
- Add explicit feature opt-in from projects and profiles.
- Add package defaults.
- Add package policy.
- Add dependency version policy.
- Add version resolution policy.
- Add package lock file design.
- Add package feature explanation output.

This phase should land before package-provided generators, asset processors, or
runtime templates.

### Phase E: Build Extensions

Purpose: add controlled build extensibility without turning `.nginproj` into a
general script engine.

- Add controlled pipeline phase model.
- Add custom command schema with declared inputs and outputs.
- Add the general `<Generators>` model.
- Convert MetaGen from a special-case `<Build><MetaGen />` feature into a
  generator contribution.
- Add package-provided generators.
- Add package-provided asset processors.

This phase depends heavily on unified inputs, stable conditions, and package
features.

### Phase F: Runtime Simplification

Purpose: make hosted/runtime applications less verbose.

- Add concise app module syntax.
- Add runtime templates through project templates or package features.
- Move common NGIN.Core wiring into package features or template defaults.
- Add profile-aware runtime feature selection.
- Clarify the runtime/build boundary.

This phase depends on model factoring, package features, and template semantics.

### Phase G: Tooling And Inspection

Purpose: make the model safe to use at scale.

- Add `ngin explain`.
- Add input explanation.
- Add condition explanation.
- Add package feature explanation.
- Add canonical normalized model output.
- Add schema-aware formatting.
- Add validation levels.
- Add graph output formats.

Some of this tooling can start earlier, but it becomes most valuable after
inputs, conditions, and packages are normalized.

### Phase H: Migration And Editor Support

Purpose: polish adoption and authoring.

- Add `ngin migrate --to-schema 3` as explicit offline tooling only.
- Add `ngin create`.
- Publish machine-readable schema files for editor completion and validation.
- Add editor support for templates, inputs, conditions, package features, and
  generators.
- Enforce the final case policy.

## Priorities

With Phase A complete, the next three changes should be:

1. Generalized project input model.
2. Package features and dependency version policy.
3. `ngin explain` for resolved model inspection.

These have the best ratio of authoring improvement to conceptual risk. They
make `.nginproj` feel more powerful and less repetitive without immediately
opening the door to arbitrary build scripting.

The next tier should be:

1. Package features and dependency version policy.
2. `ngin explain`.
3. Migration tooling.

Pipeline contributions and asset/codegen extensibility should come after the
normalized model, provenance, and explanation tooling exist. Otherwise the
extension model will be hard to debug.

## Open Questions

- Should any remaining user-facing term use `Configuration` for runtime config
  files only, or should those surfaces prefer `Settings`/`Inputs` to avoid
  confusing them with project profiles?
- Should project templates live as package assets, built-in CLI metadata, or
  workspace shared model files?
- Should dependency version policy live in `.ngin`, a separate
  `packages.nginversions`, or both?
- Should input metadata use XML attributes only, child metadata elements, or
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

This plan intentionally ignores runtime compatibility as a design constraint.
The active repository should accept the current V3 vocabulary only. If any
remaining backlog ideas become active work, they should be split into smaller
plans and converted into specs with migration rules.

Any optional V2-to-V3 migration path should be a separate, explicit offline
tooling path, not hidden fallback behavior in normal parsing or resolution. Such
a tool would require:

- automated manifest migration
- normalized-model comparison tests
- side-by-side examples
- deprecation documentation
- strict diagnostics for behavior that cannot migrate automatically
