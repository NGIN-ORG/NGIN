# Project manifest reference

A `.nginproj` describes exactly one physical source product. The root is
`Executable` or `Library`; no wrapper and no `Type` attribute exist.

```xml
<Executable Name="App" Version="1.0.0">
  <Uses><Package Name="NGIN.Base" Version="0.1" /></Uses>
  <Build><Source Include="src/**/*.cpp" /></Build>
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

## Roots

`Executable` supports Build, Uses, Generate, Tooling, Stage, Run, Test,
Benchmark, Publish, Options, and shallow When additions. It receives an
implicit default Run if no Run is authored.

`Library` requires `Kind="Static|Shared|Interface|Plugin"`. Run, Test, and
Benchmark are structurally invalid under a Library. Interface libraries cannot
contain compiled sources. Plugin means a loadable library artifact, not runtime
activation.

## Uses and versions

```xml
<Uses>
  <Package Name="Example.Security" Version="3.2">
    <Library Name="TLS" />
  </Package>
  <Project Path="../Math/Math.nginproj" />
  <Capability Name="NGIN.Net.TLS" Version="1" />
</Uses>
```

`Version` is a compatibility request; `Exact` requests one exact package
version. A bounded interval uses a child such as
`<Version AtLeast="3.2.0" Before="4.0.0" />`. Omitting typed export children
selects the package defaults. Library, Tool, Plugin, Generator, Analyzer,
Formatter, Validator, Action, and Asset selections remain typed.

## Build

Build items are repeated typed elements. Supported operations use explicit
`Include`, `Exclude`, `Remove`, and `Update` attributes. Paths are relative to
the manifest. Common items include Language, Source, Header, CxxModule,
Resource, IncludeDirectory, Define, CompileOption, LinkOption, and
PrecompiledHeader. Visibility is valid where usage propagation is meaningful.

## Conditions

`When` is additive and shallow:

```xml
<When OS="windows" Architecture="x64">
  <Uses><Package Name="Example.Windows" Version="1" /></Uses>
  <Build><Define Name="EXAMPLE_WINDOWS" /></Build>
</When>
```

Typed selectors include Configuration, Target, OS, Architecture, Toolchain,
Compiler, and `Option`/`Equals`. Attributes in one block are ANDed; every
matching block contributes. Nested When, arbitrary expressions, priority, and
specificity winners do not exist. Conflicting keyed additions are errors.

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

Selecting an action introduces its package, action export, and backing Tool in
host context. Do not repeat that package under Uses solely for activation.

## Run, Test, and Benchmark

```xml
<Run Name="server" Default="true" WorkingDirectory="content">
  <Argument>--server</Argument>
  <Environment Name="LOG_LEVEL" Value="debug" />
</Run>

<Test Name="unit" Timeout="60">
  <Argument>--reporter</Argument><Argument>console</Argument>
</Test>

<Benchmark Name="render" Timeout="120" Repetitions="5" Warmup="2" />
```

A single explicit Run is automatically the default. Multiple Runs require one
`Default="true"`. Test and Benchmark are execution registrations attached to
the same Executable product and derive distinct plans and CLI behavior.

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

Stage combines product and activated package contributions. Destination
collisions are errors. Plugin artifacts can be staged but are never loaded by
manifest semantics. Publish remains backend-neutral.

Use `ngin validate`, `ngin format --check`, and `ngin inspect --effective` to
check structural rules and inspect all lowered facts with provenance.
