---
title: Project manifest reference
description: Exact product-first structure for .nginproj executable and library manifests.
---

# Project manifest reference

A `.nginproj` describes exactly one physical product. The root is `Executable`
or `Library`; there is no generic wrapper and no root `Type` attribute.

## Roots

```xml
<Executable Name="App" Version="1.0.0">
  <Uses>
    <Package Name="NGIN.Base" Version="0.1" />
  </Uses>
  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
</Executable>
```

```xml
<Library Name="Math" Version="1.0.0" Kind="Static">
  <Build>
    <Source Include="src/**/*.cpp" />
    <Header Include="include/**/*.hpp" Visibility="Public" />
    <IncludeDirectory Path="include" Visibility="Public" />
  </Build>
</Library>
```

`Library` requires `Kind="Static|Shared|Interface|Plugin"`. Run, Test, and
Benchmark are invalid under a Library. Interface libraries cannot contain
compiled sources.

## Uses

```xml
<Uses>
  <Package Name="Example.Security" Version="3.2">
    <Library Name="TLS" />
  </Package>
  <Project Path="../Math/Math.nginproj" />
  <Capability Name="NGIN.Net.TLS" Version="1" />
</Uses>
```

Omitting typed export children selects package defaults. Versions are
compatibility requests; exact and bounded interval forms are also available.

## Build

Build contains repeated typed items such as `Language`, `Source`, `Header`,
`CxxModule`, `Resource`, `IncludeDirectory`, `Define`, `CompileOption`,
`LinkOption`, and `PrecompiledHeader`. Paths are relative to the manifest.

## Conditions

`When` is additive and shallow:

```xml
<When OS="windows" Architecture="x64">
  <Uses><Package Name="Example.Windows" Version="1" /></Uses>
  <Build><Define Name="EXAMPLE_WINDOWS" /></Build>
</When>
```

Attributes in one block are ANDed and every matching block contributes. Nested
When, arbitrary expressions, priority, and specificity winners do not exist.

## Generation and tooling

```xml
<Generate Using="NGIN.Reflection.MetaGen/ReflectionCodegen" Version="0.1">
  <Header Include="src/**/*.hpp" />
</Generate>
<Tooling>
  <Analyze Using="NGIN.Tooling.ClangTidy/Analyze" />
  <Format Using="NGIN.Tooling.ClangFormat/Format" />
</Tooling>
```

## Run, Test, and Benchmark

```xml
<Run Name="server" Default="true" WorkingDirectory="content">
  <Argument>--server</Argument>
  <Environment Name="LOG_LEVEL" Value="debug" />
</Run>
<Test Name="unit" Timeout="60" />
<Benchmark Name="render" Timeout="120" Repetitions="5" Warmup="2" />
```

An executable receives an implicit default Run. Multiple explicit Runs require
one default.

## Stage and Publish

```xml
<Stage>
  <File From="config/app.cfg" To="config/app.cfg" />
  <Directory From="content" To="content" />
</Stage>
<Publish>
  <Archive Name="portable" Format="zip" Output="dist/app.zip" />
</Publish>
```

Use `ngin validate`, `ngin format --check`, and `ngin inspect --effective` to
verify authored structure and normalized facts.
