# `.nginproj` Authoring

This guide is the practical authoring surface for NGIN project manifests.

Product-first manifests are graph-native:

```text
One project.
One primary product identity.
The workspace owns the family.
The Composition Graph owns the truth.
```

An `.nginproj` describes one primary source product. CMake is the first backend,
but the authored file describes product intent: dependencies, build inputs,
generation, staging, runtime metadata, launch, publish, tests, and profile
overlays.

## Minimal Application

```xml
<?xml version="1.0" encoding="utf-8"?>

<Project SchemaVersion="4"
         Name="Hello.Native"
         DefaultProfile="dev">
  <Application />

  <Profile Name="dev">
    <Defaults>
      <BuildType Name="Debug" />
      <TargetPlatform Name="host" />
      <Environment Name="development" />
    </Defaults>
  </Profile>
</Project>
```

The product element is the product kind. The project root is the identity.

Common product kinds:

- `Application`
- `Library`
- `Tool`
- `Test`
- `Benchmark`
- `Plugin`
- `Module`
- `External`

## Product Sections

Product sections are scoped under the product element:

```xml
<Project SchemaVersion="4" Name="Game.Server" DefaultProfile="dev">
  <Application>
    <Uses />
    <Build />
    <Generate />
    <Stage />
    <Runtime />
    <Environment />
    <Launch />
    <Publish />
  </Application>
</Project>
```

Use only the sections that matter. A plain native application can be almost
empty; an advanced hosted server can be explicit.

## Dependencies

Dependencies live under `Uses`.

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

Scopes are part of the native C++ model:

- `Build`: host tools and generators
- `Target`: compile/link inputs for the target artifact
- `Runtime`: staged or loaded runtime dependencies
- `Test`: test-only dependencies
- `Dev`: editor/analyzer/development dependencies
- `Publish`: packaging/publishing dependencies

## Publishing And Installers

`Publish` consumes the resolved staged product. Folder and ZIP outputs remain
portable; TGZ and native installers use CPack:

```xml
<Project SchemaVersion="4" Name="Game.Tool" Version="1.2.3">
  <Tool>
    <Publish Name="linux" Kind="Installer" Format="deb"
             Output="dist/Game.Tool-$(ProjectVersion).deb">
      <Include Stage="all" />
      <Installer Identifier="org.example.game-tool"
                 Vendor="Example"
                 Contact="builds@example.org"
                 Scope="Machine"
                 AddToPath="true" />
    </Publish>
  </Tool>
</Project>
```

Supported initial formats are `zip`, `tgz`, `msi`, and `deb`. MSI requires a
Windows target profile; DEB requires Linux. Installer publishing is distinct
from `.nginpack` package-store installation.

Dependency identity is the dependency name. Scope is mergeable metadata, so
profiles and workspace policy can refine the same dependency instead of adding a
second identity:

```xml
<Package Name="SDL3" Version="[3.0.0,4.0.0)" Scope="Target" />

<Profile Name="shipping">
  <Application>
    <Uses>
      <Package Name="SDL3"
               Version="[3.0.0,4.0.0)"
               AddScope="Runtime"
               RemoveScope="Target" />
    </Uses>
  </Application>
</Profile>
```

## Build Inputs

Build inputs live under `Build`.

```xml
<Build>
  <Language Standard="C++23"
            Required="true"
            Extensions="false" />

  <Sources Path="src/**.cpp" />
  <Headers Path="include/**.hpp"
           Visibility="Public" />

  <IncludePath Path="include"
               Visibility="Public" />

  <Define Name="GAME_SERVER"
          Value="1" />

  <CompileOption Value="-Wall"
                 Toolchain="gcc;clang" />

  <LinkLibrary Name="pthread"
               When="linux" />
</Build>
```

`Language Standard="C++23"` means C++23, required, with compiler extensions off
unless overridden.

Selectors can be written directly on build items:

```xml
<Build>
  <Sources Path="src/platform/windows/**.cpp"
           OperatingSystem="windows" />

  <Sources Path="src/platform/linux/**.cpp"
           OperatingSystem="linux" />

  <Define Name="GAME_DEBUG"
          Value="1"
          BuildType="Debug" />
</Build>
```

Named conditions are declared once and referenced by `When`:

```xml
<Conditions>
  <Condition Name="linux-debug">
    <All>
      <When OperatingSystem="linux" />
      <When BuildType="Debug" />
    </All>
  </Condition>
</Conditions>

<Application>
  <Build>
    <Define Name="GAME_LINUX_DEBUG"
            Value="1"
            When="linux-debug" />
  </Build>
</Application>
```

## Generation

Generators declare host tools, inputs, outputs, and arguments. Generated
outputs become normal typed graph inputs with generated provenance.

```xml
<Generate>
  <Generator Name="ReflectionMetaGen"
             Phase="Generate">
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
```

## Staging

Staging is a first-class graph product. Every staged file has a source, target,
owner, collision policy, and provenance.

```xml
<Stage>
  <Config Source="config/server.default.json"
          Target="config/server.json" />

  <Content Source="assets/**"
           Target="assets" />

  <RuntimeDependency FromPackage="SDL3"
                     Target="lib" />

  <CollisionPolicy Default="Error" />
</Stage>
```

The default collision policy is `Error`. Use explicit overrides when replacing
an inherited staged file:

```xml
<Config Source="config/server.local.json"
        Target="config/server.json"
        Collision="Override" />
```

## Runtime And Environment

`NGIN.Core` is optional. A project becomes hosted by using runtime dependencies
and runtime metadata, not by setting a global runtime flag.

```xml
<Application>
  <Uses>
    <Runtime Name="NGIN.Core"
             Version="[0.1.0,0.2.0)"
             Scope="Target;Runtime">
      <Feature Name="Profile" />
      <Feature Name="Diagnostics" />
    </Runtime>
  </Uses>

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
    <Env Name="GAME_CONFIG"
         Value="config/server.json" />

    <Secret Name="GAME_PRIVATE_TOKEN"
            From="local:game.private.token"
            Required="false" />
  </Environment>
</Application>
```

Secret values are redacted from graph JSON, diagnostics, explain output, logs,
diffs, and generated launch metadata by default.

## Profiles

Profiles are project-level overlays. A profile contains an overlay fragment for
the same product kind as the primary product.

```xml
<Profile Name="shipping">
  <Defaults>
    <BuildType Name="Release" />
    <TargetPlatform Name="linux-x64" />
    <Environment Name="production" />
  </Defaults>

  <Application>
    <Build>
      <Optimization Mode="Speed" />
      <DebugSymbols Enabled="false" />
      <LinkTimeOptimization Enabled="true" />
      <TreatWarningsAsErrors Enabled="true" />

      <Define Remove="GAME_DEBUG" />
      <Define Name="GAME_SHIPPING"
              Value="1" />
    </Build>

    <Stage>
      <Config Source="config/server.production.json"
              Target="config/server.json"
              Collision="Override" />
    </Stage>

    <Environment>
      <Env Name="GAME_ENV"
           Value="production" />
    </Environment>
  </Application>
</Profile>
```

Recommended profile names are product intents such as `dev`, `test`, `ci`, and
`shipping`. Build types remain backend concepts such as `Debug`, `Release`,
`RelWithDebInfo`, and `MinSizeRel`.

## Library Products

Libraries describe build inputs and exported product contracts.

```xml
<Project SchemaVersion="4"
         Name="Game.Engine"
         DefaultProfile="dev">
  <Library Output="Static">
    <Build>
      <Sources Path="src/**.cpp" />
      <Headers Path="include/**.hpp"
               Visibility="Public" />
      <IncludePath Path="include"
                   Visibility="Public" />
    </Build>

    <Exports>
      <Headers Path="include/**.hpp" />
      <LibraryTarget Name="Game::Engine" />
      <Capability Name="Game.Engine" />
    </Exports>

    <PackageOutput Name="Game.Engine"
                   Version="1.0.0" />
  </Library>
</Project>
```

`PackageOutput` is a package produced by this source product. It is not the
same as a consumed `Package` dependency.

## Package-Provided Tooling

Projects can opt into the official clang-tidy tool action through normal
package feature authoring. The package does not ship LLVM binaries;
it resolves `clang-tidy` from `PATH` or from the `NGIN_CLANG_TIDY` environment
variable.

```xml
<Project SchemaVersion="4"
         Name="Game.Client">
  <Application>
    <Uses>
      <Package Name="NGIN.Tooling.ClangTidy"
               Version="[0.1.0,0.2.0)"
               Scope="Dev">
        <Feature Name="Analyzer" />
      </Package>
    </Uses>

    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
```

Inspect and run the effective tooling with:

```bash
ngin tool list --project Game.Client.nginproj --profile dev
ngin tool doctor --project Game.Client.nginproj --profile dev
ngin analyze --project Game.Client.nginproj --profile dev
```

Drivers preserve the tool's intrinsic diagnostic severity. CI gating is a
separate run policy; override the contributed run by identity with
`<Policy Gate="true" FailOn="Warning" />` when warnings should fail the run.

## Workspace Imports

Current NGIN does not use `.nginmodel` as a primary file type. Shared policy lives in the
workspace and optional definition fragments:

```xml
<Workspace SchemaVersion="4"
           Name="Game"
           DefaultProfile="dev">
  <Imports>
    <Import Path="build/platforms.ngin.xml" />
    <Import Path="build/toolchains.ngin.xml" />
  </Imports>

  <Projects>
    <Project Path="Game.Engine/Game.Engine.nginproj" />
    <Project Path="Game.Server/Game.Server.nginproj" />
  </Projects>

  <Packages>
    <Source Name="local"
            Path="packages" />

    <Version Name="NGIN.Core"
             Range="[0.1.0,0.2.0)" />
  </Packages>
</Workspace>
```

## Inspecting Truth

The authored XML describes intent. The resolved Composition Graph is the truth.

Useful commands:

```bash
ngin validate --project Game.Server/Game.Server.nginproj --profile dev
ngin inspect --project Game.Server/Game.Server.nginproj --profile dev --format json
ngin graph --project Game.Server/Game.Server.nginproj --profile dev
ngin explain property:Language --project Game.Server/Game.Server.nginproj
ngin explain stage:config/server.json --project Game.Server/Game.Server.nginproj
ngin diff --project Game.Server/Game.Server.nginproj --from-profile dev --to-profile shipping
```
