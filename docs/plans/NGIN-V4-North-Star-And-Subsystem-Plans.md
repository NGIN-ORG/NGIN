# NGIN V4 Product-First Composition Graph Plan

Status: Strategic V4 Contract Proposal

## Summary

NGIN V4 is the first real NGIN product contract.

V4 is fully breaking. It does not preserve V1, V2, or V3 schema behavior for
compatibility alone. Existing ideas may be reused when they remain correct, but
legacy shape must not constrain the V4 design.

The V4 thesis:

```text
One project.
One primary product identity.
The workspace owns the family.
The Composition Graph owns the truth.
```

V4 is not a better XML manifest. V4 is a product-first, graph-native C++
project platform. XML is the first official authoring frontend. The NGIN
Composition Graph is the source of truth for build, packages, generation,
staging, runtime, tests, benchmarks, launch, publish, editor tooling, graph
diff, and explanation.

## Core Contract

### One Project, One Product

One `.nginproj` describes exactly one primary authored product identity.

A project is not a bag of targets. A project is not a generic XML container. A
project is one source product with its own lifecycle.

Valid V4 product kinds:

- `Application`
- `Library`
- `Tool`
- `Test`
- `Benchmark`
- `Plugin`
- `Module`
- `External`

Examples:

```text
Game.Engine             -> Library
Game.Client             -> Application
Game.Server             -> Application
Game.Engine.Tests       -> Test
Game.Engine.Benchmarks  -> Benchmark
Game.AssetCompiler      -> Tool
Game.AdminPlugin        -> Plugin
```

`Plugin` and `Module` are related but not identical:

- `Plugin`: a loadable artifact, usually a shared library or bundle, staged for
  discovery by an application or runtime.
- `Module`: a runtime participation unit that contributes services, startup
  behavior, capabilities, or configuration. It may or may not produce a
  separately loadable binary.

Phase one may model modules as `Runtime/Module` declarations inside
`Application`, `Library`, or `Plugin` products until a standalone `Module`
product lifecycle is proven by a real repository use case.

`External` can exist in two roles:

- external product: a project identity representing an external build or
  externally provided artifact inside the workspace graph.
- external provider: a package/provider mechanism that resolves dependencies
  from system packages, CMake packages, pkg-config, vcpkg, Conan, local paths,
  or other adapters.

Package identity is handled by `.nginpkg`. Source projects may also produce
package outputs through `PackageOutput`, but package dependencies and package
outputs are different concepts.

### Graph First

The NGIN Composition Graph is the V4 product contract.

Authored XML produces graph fragments. The resolver produces the fully explicit
graph. Backends, package restore, generators, staging, launch, runtime, tests,
benchmarks, analysis, publish, editor tooling, graph diff, and explain commands
consume the same graph.

CMake remains the first backend, but it is not the user's mental model.

### Runtime Optional

`NGIN.Core` remains optional. Plain native C++ projects are first-class.

Hosted runtime projects opt in by using runtime-oriented product sections and
dependencies:

- `Runtime` dependency inside `Uses`
- `Runtime` product section
- `RuntimeModule`
- `RuntimeConfig` or staged runtime content
- `Plugin`
- `RuntimeSetting`
- `Env`, `LaunchEnv`, and `Secret`

There is no `UseRuntime=true` flag in the recommended V4 shape.

## Mental Model

```text
Workspace        repository/product-family policy
Project          one primary authored product identity
Product          the thing produced by the project
Profile          named overlay for build/run/test/publish intent
Package          independently versioned reusable product contract
CompositionGraph resolved truth across projects, packages, tools, runtime,
                 stage, launch, tests, publish, editor, trust, provenance
Backend          implementation detail, initially CMake
```

## File Types

V4 should use a small, strong file set:

- `.nginproj`: one source product project
- `.nginpkg`: reusable package identity/export/features contract
- `.ngin`: workspace policy and project grouping
- `.ngin.xml`: imported definition fragment, optional
- `.nginlaunch`: generated local launch metadata
- `.nginpack`: immutable package archive
- `ngin.lock`: resolved package graph and restore lock

`.nginmodel` is not a primary V4 concept. Shared defaults, platforms,
toolchains, profile templates, feature maps, and other definitions are ordinary
workspace imports:

```xml
<Import Path="build/platforms.ngin.xml" />
<Import Path="build/toolchains.ngin.xml" />
<Import Path="build/profiles.ngin.xml" />
```

Imported files are graph fragments, not a separate model layer.

## Product-First Project Shape

### Minimal Application

```xml
<?xml version="1.0" encoding="utf-8"?>

<Project SchemaVersion="4" Name="Hello.Native">
  <Application />
</Project>
```

Default graph contributions:

```text
ProductKind         Application
OutputType          Executable
OutputName          Hello.Native
Language            CXX
LanguageStandard    23
StandardRequired    true
CompilerExtensions  false
Backend             CMake
BuildMode           Generated
SourceRoot          src
HeaderRoot          include, if present
DefaultProfile      dev
BuildType           Debug
HostPlatform        host
TargetPlatform      host
StageRoot           build/<profile>/stage
Launch              Hello.Native from staged directory
Runtime             none
```

These are not hidden resolver constants. They are graph contributions from
named conventions:

- `NGIN.Application`
- `NGIN.Cpp.Defaults`
- `NGIN.Profile.dev`
- `NGIN.HostPlatform`
- `NGIN.CMake.Generated`

`ngin explain property:Language` should show the convention that supplied the
language default and the exact authoring shape that would override it.

### Minimal Library

```xml
<?xml version="1.0" encoding="utf-8"?>

<Project SchemaVersion="4" Name="Game.Engine">
  <Library />
</Project>
```

Default graph contributions:

```text
ProductKind         Library
OutputType          StaticLibrary
SourceRoot          src
PublicHeaderRoot    include, if present
PublicIncludePath   include, if present
Launch              none
PackageExportable   true
```

### Minimal Test

```xml
<?xml version="1.0" encoding="utf-8"?>

<Project SchemaVersion="4" Name="Game.Engine.Tests">
  <Test>
    <Uses>
      <Project Name="Game.Engine"
               Path="../Game.Engine/Game.Engine.nginproj" />

      <Package Name="Catch2"
               Version="[3.0.0,4.0.0)"
               Scope="Test" />
    </Uses>
  </Test>
</Project>
```

Default graph contributions:

```text
ProductKind         Test
OutputType          Executable
SourceRoot          src
RunCommand          produced test executable
TestCommand         ngin test
Launch              test run entry, not normal app launch
```

## Product Sections

Each product supports a constrained set of sections. The schema should reject
sections that do not make sense for a product unless a future extension
explicitly allows them.

Common product sections, where meaningful:

- `Uses`
- `Build`
- `Generate`
- `Stage`
- `Environment`

Not every product kind accepts every common section. For example, a `Library`
usually does not need a stage layout, but may support `Stage` when it owns
runtime assets, package content, or generated resources that require staging.

Profiles are project-level overlays. A project may contain zero or more
`Profile` elements after the primary product element. Each `Profile` contains an
overlay fragment for the same product kind as the project's primary product,
unless a future extension explicitly permits cross-product policy fragments.

Application sections:

- `Runtime`
- `Launch`
- `Publish`

Library sections:

- `Exports`
- `PackageOutput`

Tool sections:

- `Run`
- `Stage`
- `PackageOutput`

Test sections:

- `Run`
- `Report`
- `TestSettings`

Benchmark sections:

- `Run`
- `Report`
- `BenchmarkSettings`

Plugin sections:

- `Runtime`
- `Stage`
- `Exports`
- `PackageOutput`

Module sections:

- `Runtime`
- `Exports`
- `PackageOutput`

External sections:

- `Uses`
- `Exports`
- `Stage`
- `PackageOutput`

## Dependency Model

Products consume dependencies through `Uses`.

V4 item attributes should follow one naming style:

- `Name`: logical identity
- `Path`: filesystem input path or glob
- `Source`: staged/copied source path
- `Target`: staged/copied destination path
- `Value`: literal compiler, linker, environment, or argument value

```xml
<Uses>
  <Project Name="Game.Engine"
           Path="../Game.Engine/Game.Engine.nginproj" />

  <Package Name="SDL3"
           Version="[3.0.0,4.0.0)"
           Scope="Target;Runtime" />

  <Tool Name="NGIN.Reflection.MetaGen"
        Version="[0.1.0,0.2.0)"
        Scope="Build" />

  <Runtime Name="NGIN.Core"
           Version="[0.1.0,0.2.0)"
           Scope="Target;Runtime">
    <Feature Name="Diagnostics" />
    <Feature Name="Reflection" />
  </Runtime>
</Uses>
```

Dependency scopes:

- `Build`: runs on `HostPlatform`; tools, generators, codegen
- `Target`: used to compile or link the target artifact
- `Runtime`: staged or loaded at runtime
- `Test`: used only by test products
- `Dev`: editor/analyzer/development-only dependency
- `Publish`: used only for packaging/publishing

This scope model is not optional for native C++. A generator may run on
`windows-x64` while generating code for `linux-arm64`. The graph must keep host,
target, runtime, test, development, and publish closures separate.

Dependency identity is dependency kind plus name:

```text
Package  SDL3
Tool     NGIN.Reflection.MetaGen
Runtime  NGIN.Core
```

Scope is mergeable metadata on that dependency identity, not part of the
identity. Re-declaring the same dependency merges compatible scopes, features,
and selectors. Conflicting versions, providers, or incompatible feature
selections are diagnostics unless an explicit override policy is selected.

Example:

```xml
<Package Name="SDL3" Scope="Target" />
<Package Name="SDL3" Scope="Target;Runtime" />
```

These declarations refer to one `Package/SDL3` dependency whose effective scope
includes `Target` and `Runtime`, subject to version/provider compatibility.

Profiles and workspace policy may mutate scopes explicitly:

```xml
<Package Name="SDL3" AddScope="Runtime" />
<Package Name="SDL3" RemoveScope="Target" />
```

These are mutations on the same `Package/SDL3` identity.

## Package Resolution Policy

Package resolution is graph-level policy.

A product may request a version range. A workspace may centralize or constrain
package versions, feeds, providers, and trust policy. The resolver selects one
version per package identity per compatible closure unless side-by-side versions
are explicitly supported for that package kind.

The resolved graph and lock file record, for every package:

- requested ranges
- selected version
- rejected alternatives when useful for diagnostics
- provider
- source/feed
- hash
- selected features
- dependency scope closure
- compatibility match
- provenance

## Host, Target, Runtime, And Package Platforms

V4 must never collapse host and target.

```xml
<Defaults>
  <HostPlatform Name="host" />
  <TargetPlatform Name="linux-x64" />
</Defaults>
```

The graph should distinguish:

- `HostPlatform`: where build tools and generators run
- `TargetPlatform`: what is being built
- `RuntimePlatform`: where staged app is intended to run, if different
- `PackagePlatform`: selected package binary variant
- `AbiTag`: binary compatibility identity

Example binary compatibility tags:

```text
linux-x64-clang18-libc++-cxx23-release
windows-x64-msvc-v143-md-cxx23-release
macos-arm64-appleclang-libc++-cxx23-release
```

Package selection must use these tags, not only OS and architecture.

## Profiles

Profiles are product-aware overlays. They are not generic `Debug`/`Release`
aliases.

A profile may affect:

- build type
- optimization
- toolchain
- host platform
- target platform
- environment
- defines
- package features
- generator behavior
- staged files
- runtime modules
- launch arguments
- tests
- analyzers
- publish artifacts
- package policy
- trust policy

Recommended profile names:

- `dev`
- `test`
- `ci`
- `shipping`

Build types remain backend concepts:

- `Debug`
- `Release`
- `RelWithDebInfo`
- `MinSizeRel`

Example profile overlay:

```xml
<Profile Name="shipping">
  <Application>
    <Build>
      <Type>Release</Type>
      <Optimization Mode="Speed" />
      <DebugSymbols Enabled="false" />
      <LinkTimeOptimization Enabled="true" />
      <TreatWarningsAsErrors Enabled="true" />

      <Define Remove="GAME_DEBUG" />
      <Define Remove="GAME_ENABLE_DEV_CONSOLE" />
      <Define Name="GAME_SHIPPING" Value="1" />
    </Build>

    <Runtime>
      <Plugin Remove="Game.DebugTools" />
      <Module Remove="Game.Server.Diagnostics" />
    </Runtime>

    <Stage>
      <Content Remove="assets/debug/**" />
    </Stage>

    <Environment>
      <Env Name="GAME_ENV" Value="production" />
    </Environment>

    <Publish Name="archive"
             Kind="Archive"
             Format="zip"
             Output="dist/Game.Client.zip">
      <Include Stage="all" />
      <Include RuntimeDependencies="true" />
    </Publish>
  </Application>
</Profile>
```

## Overlay, Identity, And Provenance

V4 overlay rules:

```text
Scalar values:
  nearest selected declaration wins.

Collections:
  append by default.

Items:
  identity determines replace/remove/collision behavior.

Remove:
  removes matching inherited item before local additions.

Override:
  explicitly replaces an inherited item with the same identity.

Duplicates:
  errors unless item type explicitly allows multi-values.

Every decision:
  stores provenance.
```

Initial item identities:

```text
Project          Name when present, otherwise normalized Path
Package          Name
Tool             Name
Runtime          Name
Feature          owning dependency + feature name
Source           normalized path/glob + role
Header           normalized path/glob + visibility
Define           Name
IncludePath      normalized path + visibility
CompileOption    Value + toolchain selector
LinkLibrary      Name + target platform selector
Config           Target when present, otherwise Source
Content          Target when present, otherwise Source
RuntimeModule    Name
Plugin           Name
Env              Name
LaunchEnv        Name
RuntimeSetting   Name
Secret           Name or target Env
Launch           Name
Publish          Name
Generator        Name
TestRun          Name
BenchmarkRun     Name
PackageOutput    Name
```

Example:

```xml
<Define Name="GAME_MODE" Value="debug" />
<Define Name="GAME_MODE" Value="shipping" />
```

Both items have identity `Define/GAME_MODE`, so V4 can replace, override, or
diagnose according to overlay context.

Staging example:

```xml
<Config Source="config/default.json"
        Target="config/app.json" />

<Config Source="config/local.json"
        Target="config/app.json"
        Collision="Override" />
```

Both items have staging identity `stage:config/app.json`, so
`ngin explain stage:config/app.json` can tell the full story.

Default stage collision policy is `Error`.

Secret values are redacted by default from graph JSON, diagnostics, logs, launch
metadata, explain output, and diffs. The graph records secret identity,
requirement, target environment mapping, source kind, and provenance, but not
the raw value in normal outputs.

Generated outputs become normal typed graph inputs. Generated sources and
headers participate in the same build graph as authored sources, with
`Generated=true` and generator provenance. This makes generated files
explainable:

```bash
ngin explain source:generated/reflection/Foo.generated.cpp
```

## Composition Graph

The graph is the V4 public contract. Authored XML produces graph fragments. The
resolver produces a fully explicit graph.

Graph facets:

- identity
- workspace
- project
- product
- profile
- platform
- toolchain
- package
- build
- generate
- stage
- runtime
- environment
- launch
- test
- benchmark
- publish
- editor
- trust
- diagnostics
- provenance

Required graph outputs:

```bash
ngin graph
ngin inspect --format json
ngin graph --build-plan
ngin graph --stage-plan
ngin graph --package-plan
ngin graph --launch-plan
ngin graph --runtime-plan
ngin diff
ngin explain ...
```

Sketch:

```json
{
  "schemaVersion": "4.0",
  "project": {
    "name": "Game.Client",
    "path": "Game.Client/Game.Client.nginproj"
  },
  "product": {
    "kind": "Application",
    "outputType": "Executable",
    "outputName": "Game.Client"
  },
  "selection": {
    "profile": "dev",
    "hostPlatform": "host",
    "targetPlatform": "linux-x64",
    "toolchain": "clang-lld"
  },
  "build": {
    "language": "CXX",
    "languageStandard": 23,
    "standardRequired": true,
    "compilerExtensions": false,
    "backend": "CMake",
    "sources": [
      {
        "path": "src/main.cpp",
        "role": "Source",
        "provenance": "Game.Client.nginproj:/Project/Application/Build/Sources"
      }
    ],
    "defines": [
      {
        "name": "GAME_CLIENT",
        "value": "1",
        "visibility": "Private"
      }
    ]
  },
  "stage": {
    "files": [
      {
        "source": "config/client.local.json",
        "target": "config/client.json",
        "collision": "Override"
      }
    ]
  },
  "launch": [
    {
      "name": "client",
      "workingDirectory": "$(StageDir)",
      "args": "--config config/client.json --dev-console"
    }
  ]
}
```

## Workspace

A workspace defines repository/product-family policy and groups related
projects. It does not replace project identity.

```xml
<?xml version="1.0" encoding="utf-8"?>

<Workspace SchemaVersion="4"
           Name="Game"
           DefaultProfile="dev">

  <Imports>
    <Import Path="build/platforms.ngin.xml" />
    <Import Path="build/toolchains.ngin.xml" />
    <Import Path="build/profiles.ngin.xml" />
  </Imports>

  <Projects>
    <Project Path="Game.Engine/Game.Engine.nginproj" />
    <Project Path="Game.Engine.Tests/Game.Engine.Tests.nginproj" />
    <Project Path="Game.Engine.Benchmarks/Game.Engine.Benchmarks.nginproj" />
    <Project Path="Game.Client/Game.Client.nginproj" />
    <Project Path="Game.Server/Game.Server.nginproj" />
    <Project Path="Game.AssetCompiler/Game.AssetCompiler.nginproj" />
    <Project Path="Game.AdminPlugin/Game.AdminPlugin.nginproj" />
  </Projects>

  <Defaults>
    <Language Standard="C++23"
              Required="true"
              Extensions="false" />

    <Backend Name="CMake"
             Mode="Generated" />

    <HostPlatform Name="host" />
    <TargetPlatform Name="host" />

    <GeneratedRoot Path="$(WorkspaceOutputDir)/generated" />
    <StageRoot Path="$(WorkspaceOutputDir)/stage" />
    <PackageOutputRoot Path="$(WorkspaceOutputDir)/packages" />
  </Defaults>

  <Packages>
    <Source Name="local"
            Path="packages" />

    <Source Name="ngin"
            Url="https://packages.ngin.dev/v1/index.json" />

    <Version Name="NGIN.Core"
             Range="[0.1.0,0.2.0)" />

    <Version Name="NGIN.Reflection.MetaGen"
             Range="[0.1.0,0.2.0)" />

    <Version Name="Catch2"
             Range="[3.0.0,4.0.0)" />
  </Packages>

  <Profiles>
    <Profile Name="dev">
      <Defaults>
        <BuildType Name="Debug" />
        <Environment Name="development" />
        <Toolchain Name="clang-lld" />
      </Defaults>
    </Profile>

    <Profile Name="ci">
      <Defaults>
        <BuildType Name="Debug" />
        <Environment Name="ci" />
        <Toolchain Name="clang-lld" />
      </Defaults>

      <Quality>
        <TreatWarningsAsErrors Enabled="true" />
        <Analyzer Name="clang-tidy" Enabled="true" />
      </Quality>
    </Profile>

    <Profile Name="shipping">
      <Defaults>
        <BuildType Name="Release" />
        <Environment Name="production" />
        <Toolchain Name="clang-lld" />
      </Defaults>

      <Build>
        <Optimization Mode="Speed" />
        <LinkTimeOptimization Enabled="true" />
        <DebugSymbols Enabled="false" />
      </Build>
    </Profile>
  </Profiles>
</Workspace>
```

## Definition Fragments

Definition fragments use `.ngin.xml`.

Platform definitions:

```xml
<?xml version="1.0" encoding="utf-8"?>

<Definitions SchemaVersion="4" Name="Game.Platforms">
  <Platforms>
    <Platform Name="linux-x64"
              OperatingSystem="linux"
              Architecture="x64"
              Abi="linux-x64-clang18-libc++-cxx23" />

    <Platform Name="windows-x64"
              OperatingSystem="windows"
              Architecture="x64"
              Abi="windows-x64-msvc-v143-md-cxx23" />
  </Platforms>
</Definitions>
```

Toolchain definitions:

```xml
<?xml version="1.0" encoding="utf-8"?>

<Definitions SchemaVersion="4" Name="Game.Toolchains">
  <Toolchains>
    <Toolchain Name="clang-lld"
               Compiler="clang"
               CompilerVersion="18"
               Linker="lld"
               Generator="Ninja"
               CppStandardLibrary="libc++" />

    <Toolchain Name="msvc-v143"
               Compiler="msvc"
               CompilerVersion="v143"
               Linker="link"
               Generator="Ninja"
               RuntimeLibrary="MD" />
  </Toolchains>
</Definitions>
```

## Full Product Examples

### Library

```xml
<?xml version="1.0" encoding="utf-8"?>

<Project SchemaVersion="4"
         Name="Game.Engine"
         DefaultProfile="dev">

  <Library Output="Static">
    <Uses>
      <Package Name="fmt"
               Version="[10.0.0,11.0.0)"
               Scope="Target" />

      <Package Name="glm"
               Version="[1.0.0,2.0.0)"
               Scope="Target" />
    </Uses>

    <Build>
      <Sources Path="src/**.cpp" />

      <Headers Path="include/**.hpp"
               Visibility="Public" />

      <Exclude Path="src/Experimental/**" />

      <IncludePath Path="include"
                   Visibility="Public" />

      <IncludePath Path="src"
                   Visibility="Private" />

      <Define Name="GAME_ENGINE"
              Value="1"
              Visibility="Public" />

      <CompileOption Value="-Wall"
                     Toolchain="gcc;clang" />

      <CompileOption Value="/W4"
                     Toolchain="msvc" />
    </Build>

    <Exports>
      <Headers Path="include/**.hpp" />
      <LibraryTarget Name="Game::Engine" />
      <Capability Name="Game.Engine" />
    </Exports>

    <PackageOutput Name="Game.Engine"
                   Version="1.0.0">
      <Metadata>
        <Description>Core engine library for the Game workspace.</Description>
        <License>MIT</License>
      </Metadata>

      <Compatibility>
        <Language Standard="C++23" />
        <TargetPlatform Name="linux-x64" />
        <TargetPlatform Name="windows-x64" />
        <Abi Tag="$(ResolvedAbiTag)" />
      </Compatibility>
    </PackageOutput>
  </Library>

  <Profile Name="shipping">
    <Library>
      <Build>
        <Type>Release</Type>
        <Optimization Mode="Speed" />
        <DebugSymbols Enabled="false" />
        <LinkTimeOptimization Enabled="true" />

        <Define Remove="GAME_ENGINE_DEBUG" />
        <Define Name="GAME_ENGINE_SHIPPING" Value="1" />
      </Build>
    </Library>
  </Profile>
</Project>
```

### Application

```xml
<?xml version="1.0" encoding="utf-8"?>

<Project SchemaVersion="4"
         Name="Game.Client"
         DefaultProfile="dev">

  <Application>
    <Uses>
      <Project Name="Game.Engine"
               Path="../Game.Engine/Game.Engine.nginproj" />

      <Package Name="SDL3"
               Version="[3.0.0,4.0.0)"
               Scope="Target;Runtime" />

      <Tool Name="Game.AssetCompiler"
            Path="../Game.AssetCompiler/Game.AssetCompiler.nginproj"
            Scope="Build" />
    </Uses>

    <Build>
      <Language Standard="C++23" />
      <Sources Path="src/**.cpp" />
      <Headers Path="include/**.hpp" Visibility="Private" />
      <IncludePath Path="include" Visibility="Private" />
      <Define Name="GAME_CLIENT" Value="1" />
      <PrecompiledHeader Path="include/Game/Client/Pch.hpp" />
      <UnityBuild Enabled="false" />
    </Build>

    <Generate>
      <Generator Name="CompileAssets" Phase="Generate">
        <Tool Name="Game.AssetCompiler" />

        <Inputs>
          <Files Path="../Assets/raw/**" />
        </Inputs>

        <Outputs>
          <Files Path="$(GeneratedDir)/assets/**" />
        </Outputs>

        <Args>
          <Arg Value="--input" />
          <Arg Path="../Assets/raw" />
          <Arg Value="--output" />
          <Arg Path="$(GeneratedDir)/assets" />
        </Args>
      </Generator>
    </Generate>

    <Stage>
      <Config Source="config/client.default.json"
              Target="config/client.json" />

      <Content Source="$(GeneratedDir)/assets/**"
               Target="assets" />

      <RuntimeDependency FromPackage="SDL3"
                         Target="lib" />

      <CollisionPolicy Default="Error" />
    </Stage>

    <Environment>
      <Env Name="GAME_CONFIG" Value="config/client.json" />

      <Secret Name="GAME_TELEMETRY_TOKEN"
              From="local:game.telemetry.token"
              Required="false" />
    </Environment>

    <Launch Name="client"
            WorkingDirectory="$(StageDir)"
            Args="--config config/client.json" />

    <Publish Name="folder"
             Kind="Folder"
             Output="dist/Game.Client">
      <Include Stage="all" />
      <Include RuntimeDependencies="true" />
      <Include Symbols="false" />
    </Publish>
  </Application>

  <Profile Name="dev">
    <Application>
      <Build>
        <Type>Debug</Type>
        <Optimization Mode="Off" />
        <DebugSymbols Enabled="true" />

        <Sanitizer Name="Address"
                   When="linux" />

        <Define Name="GAME_DEBUG" Value="1" />
        <Define Name="GAME_ENABLE_DEV_CONSOLE" Value="1" />
      </Build>

      <Stage>
        <Config Source="config/client.local.json"
                Target="config/client.json"
                Collision="Override" />
      </Stage>

      <Environment>
        <Env Name="GAME_ENV" Value="development" />
      </Environment>

      <Launch Name="client"
              Args="--config config/client.json --dev-console" />
    </Application>
  </Profile>
</Project>
```

### Hosted Runtime Application

```xml
<?xml version="1.0" encoding="utf-8"?>

<Project SchemaVersion="4"
         Name="Game.Server"
         DefaultProfile="dev">

  <Application>
    <Uses>
      <Project Name="Game.Engine"
               Path="../Game.Engine/Game.Engine.nginproj" />

      <Runtime Name="NGIN.Core"
               Version="[0.1.0,0.2.0)"
               Scope="Target;Runtime">
        <Feature Name="Profile" />
        <Feature Name="Diagnostics" />
      </Runtime>

      <Tool Name="NGIN.Reflection.MetaGen"
            Version="[0.1.0,0.2.0)"
            Scope="Build" />
    </Uses>

    <Build>
      <Sources Path="src/**.cpp" />
      <Headers Path="include/**.hpp" />
      <IncludePath Path="include" />
      <Define Name="GAME_SERVER" Value="1" />
      <LinkLibrary Name="pthread" When="linux" />
      <LinkLibrary Name="ws2_32" When="windows" />
    </Build>

    <Generate>
      <Generator Name="ReflectionMetaGen" Phase="Generate">
        <Tool Name="NGIN.Reflection.MetaGen"
              Executable="ngin-metagen" />

        <Inputs>
          <Headers Path="include/**.hpp" />
        </Inputs>

        <Outputs>
          <Sources Path="$(GeneratedDir)/reflection/**.generated.cpp" />
          <Headers Path="$(GeneratedDir)/reflection/**.generated.hpp" />
        </Outputs>

        <Context Path="$(GeneratedDir)/ReflectionMetaGen.context.json" />

        <Args>
          <Arg Value="--context" />
          <Arg Path="$(GeneratedDir)/ReflectionMetaGen.context.json" />
        </Args>
      </Generator>
    </Generate>

    <Stage>
      <Config Source="config/server.default.json"
              Target="config/server.json" />

      <Content Source="runtime/**"
               Target="runtime" />

      <Plugin Name="Game.AdminPlugin"
              Target="plugins/admin"
              Load="Optional" />

      <CollisionPolicy Default="Error" />
    </Stage>

    <Runtime>
      <Module Name="Game.Server.Startup"
              Stage="Startup"
              Order="100">
        <Requires Service="NGIN.Core.Profile" />
        <Provides Service="Game.Server.Ready" />
      </Module>

      <Setting Name="game.config"
               Value="config/server.json" />
    </Runtime>

    <Environment>
      <Env Name="GAME_CONFIG" Value="config/server.json" />

      <Secret Name="GAME_PRIVATE_TOKEN"
              From="local:game.private.token"
              Required="false" />
    </Environment>

    <Launch Name="server"
            WorkingDirectory="$(StageDir)"
            Args="--config config/server.json" />
  </Application>
</Project>
```

### Tool

```xml
<?xml version="1.0" encoding="utf-8"?>

<Project SchemaVersion="4" Name="Game.AssetCompiler">
  <Tool>
    <Uses>
      <Project Name="Game.Engine"
               Path="../Game.Engine/Game.Engine.nginproj" />
    </Uses>

    <Build>
      <Sources Path="src/**.cpp" />
      <Headers Path="include/**.hpp" />
      <Define Name="GAME_ASSET_COMPILER" Value="1" />
    </Build>

    <Stage>
      <Content Source="templates/**" Target="templates" />
    </Stage>

    <Run Name="compile-example-assets"
         WorkingDirectory="$(StageDir)"
         Args="--input ../Assets/raw --output ../Assets/compiled" />

    <PackageOutput Name="Game.AssetCompiler"
                   Version="1.0.0">
      <Exports>
        <Tool Name="Game.AssetCompiler"
              Executable="$(OutputName)" />
      </Exports>
    </PackageOutput>
  </Tool>
</Project>
```

### Test

```xml
<?xml version="1.0" encoding="utf-8"?>

<Project SchemaVersion="4"
         Name="Game.Engine.Tests"
         DefaultProfile="dev">
  <Test>
    <Uses>
      <Project Name="Game.Engine"
               Path="../Game.Engine/Game.Engine.nginproj" />

      <Package Name="Catch2"
               Version="[3.0.0,4.0.0)"
               Scope="Test" />
    </Uses>

    <Build>
      <Sources Path="src/**.cpp" />
      <Define Name="GAME_ENGINE_TESTS" Value="1" />
    </Build>

    <Run Name="unit"
         WorkingDirectory="$(StageDir)"
         Args="--reporter console --durations yes" />

    <Report Kind="JUnit"
            Output="$(OutputDir)/test-results/Game.Engine.Tests.xml" />
  </Test>

  <Profile Name="ci">
    <Test>
      <Run Name="unit"
           Args="--reporter junit --out $(OutputDir)/test-results/Game.Engine.Tests.xml" />
    </Test>
  </Profile>
</Project>
```

### Plugin

```xml
<?xml version="1.0" encoding="utf-8"?>

<Project SchemaVersion="4" Name="Game.AdminPlugin">
  <Plugin For="Game.Server">
    <Uses>
      <Project Name="Game.Engine"
               Path="../Game.Engine/Game.Engine.nginproj" />

      <Runtime Name="NGIN.Core"
               Version="[0.1.0,0.2.0)"
               Scope="Target;Runtime" />
    </Uses>

    <Build>
      <Sources Path="src/**.cpp" />
      <Headers Path="include/**.hpp" />
      <Define Name="GAME_ADMIN_PLUGIN" Value="1" />
    </Build>

    <Runtime>
      <Module Name="Game.AdminPlugin.Module"
              Stage="Features"
              Order="500">
        <Requires Service="Game.Server.Ready" />
        <Provides Service="Game.AdminTools" />
      </Module>
    </Runtime>

    <Stage>
      <Config Source="config/admin.default.json"
              Target="plugins/admin/config/admin.json" />
    </Stage>

    <Exports>
      <Plugin Name="Game.AdminPlugin"
              Target="plugins/admin" />
    </Exports>
  </Plugin>
</Project>
```

## Package Manifest

Standalone package manifests describe reusable package identity, exports, and
features. They are not source build recipes.

```xml
<?xml version="1.0" encoding="utf-8"?>

<Package SchemaVersion="4"
         Name="NGIN.Core"
         Version="0.1.0">

  <Metadata>
    <Description>Optional hosted runtime for NGIN applications.</Description>
    <License>MIT</License>
    <Author>NGIN-ORG</Author>
  </Metadata>

  <Library Name="NGIN.Core">
    <Exports>
      <Headers Path="include/**.hpp" />

      <Binary Path="lib/linux-x64/libNGIN.Core.a"
              TargetPlatform="linux-x64"
              Abi="linux-x64-clang18-libc++-cxx23" />

      <Binary Path="lib/windows-x64/NGIN.Core.lib"
              TargetPlatform="windows-x64"
              Abi="windows-x64-msvc-v143-md-cxx23" />

      <LibraryTarget Name="NGIN::Core" />
      <Capability Name="NGIN.Runtime" />
    </Exports>
  </Library>

  <Features>
    <Feature Name="Profile">
      <Runtime>
        <Provides Service="NGIN.Core.Profile" />
      </Runtime>
    </Feature>

    <Feature Name="Diagnostics">
      <Build>
        <Define Name="NGIN_CORE_DIAGNOSTICS"
                Value="1"
                Visibility="Public" />
      </Build>

      <Runtime>
        <Provides Service="NGIN.Core.Diagnostics" />
      </Runtime>
    </Feature>

    <Feature Name="Reflection">
      <Uses>
        <Tool Name="NGIN.Reflection.MetaGen"
              Version="[0.1.0,0.2.0)"
              Scope="Build" />
      </Uses>

      <Build>
        <Define Name="NGIN_CORE_REFLECTION"
                Value="1"
                Visibility="Public" />
      </Build>

      <Provides>
        <Capability Name="Reflection" />
      </Provides>
    </Feature>
  </Features>
</Package>
```

## CLI Contract Direction

Core commands:

```bash
ngin new app Hello.Native
ngin new lib Game.Engine
ngin new test Game.Engine.Tests --uses Game.Engine

ngin validate
ngin restore
ngin configure
ngin build
ngin stage
ngin run
ngin test
ngin benchmark
ngin analyze
ngin format
ngin publish

ngin graph
ngin inspect --format json
ngin diff
ngin explain ...
```

Package commands:

```bash
ngin package add NGIN.Core --version "[0.1.0,0.2.0)"
ngin package remove NGIN.Core
ngin package update NGIN.Core
ngin package pack
ngin package publish

ngin package sources list
ngin package sources add ngin https://packages.ngin.dev/v1/index.json
ngin package sources remove ngin
```

Explain commands should use object identity syntax:

```bash
ngin explain property:Language
ngin explain source:src/main.cpp
ngin explain define:GAME_SHIPPING
ngin explain package:SDL3
ngin explain feature:NGIN.Core/Reflection
ngin explain stage:config/client.json
ngin explain generator:CompileAssets
ngin explain launch:client
ngin explain runtime-module:Game.Server.Startup
ngin explain toolchain:clang-lld
```

Diff:

```bash
ngin diff \
  --project Game.Client/Game.Client.nginproj \
  --from-profile dev \
  --to-profile shipping
```

## Implementation Order

The active V3 specs are not the V4 shape. V4 should introduce a clean parser
that feeds the new graph instead of decorating the V3 root sections.

Recommended order:

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

## Open Decisions

- Whether XML remains the only official V4 frontend in phase one.
- Whether standalone `Module` products ship in phase one, or phase one models
  modules only as runtime declarations inside other products.
- Whether phase-one `External` support starts with external products, external
  providers, or both.
- Whether workspace profile overlays can target all products of a kind, named
  projects only, or both.
- Exact graph JSON schema shape and versioning policy.
- Exact `.nginpack`, static feed, lock file, and package store formats.
- Exact ABI tag dimensions per platform/toolchain family.
- Whether source projects may emit multiple `PackageOutput` entries.
- Which quality policies are built in for phase one.
- Whether `ngin stage` is public in phase one or remains part of `ngin build`.

## Definition Of Done

V4 is not done when the parser accepts `SchemaVersion="4"`. V4 is done when:

- one project equals one primary product identity
- minimal native apps are genuinely tiny
- advanced products remain explicit and inspectable
- workspace policy groups related products without stealing product identity
- packages restore deterministically
- source and binary package modes share one dependency declaration
- build, target, runtime, test, dev, and publish dependency scopes are modeled
- host and target package closures are separate
- CMake generation comes from the Composition Graph
- generated files are hermetic and declared
- staged output is collision-aware and explainable
- graph diff makes profile and package changes reviewable
- tests, benchmarks, analyzers, and publish are part of normal workflow
- editor tooling consumes the same graph as the CLI
- active docs teach V4 as the normal path
