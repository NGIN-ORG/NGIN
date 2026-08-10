# Manifest authoring model

Status: Normative pre-release contract

This document defines the XML authoring and semantic model implemented by NGIN.
Before the first official manifest release there is one accepted grammar and no
authored format number. Superseded project wrappers, profiles, features, scope
strings, and runtime-module declarations are invalid.

## Principles

- XML is the only authored manifest format.
- One project describes one primary product.
- Project and package semantics are independent of CMake.
- Package resolution and export activation are separate.
- Every semantic category has its own merge law.
- Configuration, Target, Toolchain, and typed Options are the complete build
  selection model.
- Presets expand before semantic resolution and do not affect graph identity.
- Application runtime composition belongs to C++ and optional runtime
  frameworks, not manifests.
- The immutable Composition Graph contains semantic truth only.
- Backend integration bindings and executable plans are immutable sidecars
  derived without rereading XML.
- Unknown core names and unregistered extension namespaces are errors.

## Document kinds

| Extension | Root | Responsibility |
| --- | --- | --- |
| `.nginproj` | `Project` | One product, its direct inputs, dependencies, actions, deployment, and command intent |
| `.nginpkg` | `Package` | One package release, public options, requirements, named exports, contributions, compatibility, and integration metadata |
| `.ngin` | `Workspace` | Discovery, shared defaults, package sources, central constraints, Targets, Toolchains, policy, and Presets |

Core elements use no XML namespace. Backend integrations use registered
namespaces. XML declarations are optional; when present, UTF-8 is required.

## Common scalar rules

- Names are case-sensitive Unicode strings normalized to NFC.
- Public identities use ASCII letters, digits, `.`, `_`, and `-`; they begin
  with a letter or digit.
- Boolean values are exactly `true` or `false`.
- Enum values are case-sensitive and use their documented spelling.
- Empty names, versions, paths, and option values are rejected unless a field
  explicitly allows an empty string.
- Duplicate scalar assignments at one authority are errors.
- Unknown elements or attributes are errors even when they could be ignored
  safely.
- XML child order has no semantic effect unless this document explicitly calls
  a collection ordered.

## Project manifest

### Root

```xml
<Project Name="Gallery"
         Type="Application"
         Version="0.4.0">
</Project>
```

| Attribute | Required | Meaning |
| --- | --- | --- |
| `Name` | yes | Product identity within the workspace |
| `Type` | yes | `Application`, `Library`, `Tool`, `Test`, `Benchmark`, `Plugin`, or `External` |
| `Version` | no | Product release version; required by publishing formats that need one |

`SchemaVersion`, `DefaultProfile`, and backend attributes are invalid.

There is no `Module` product. Use:

- `Library` for linked reusable code;
- `Plugin` for a dynamically loaded binary;
- `CxxModule` build items for C++ language module units.

### Direct sections

| Section | Cardinality | Purpose |
| --- | --- | --- |
| `Metadata` | zero or one | Description, license, homepage, vendor, and release metadata |
| `Options` | zero or one | Typed project option declarations and defaults |
| `Dependencies` | zero or one | Direct project/package dependencies and export activation |
| `Build` | zero or one | Language, source, header, module, resource, and build requirements |
| `Generate` | zero or more | Explicit Generate Action selections |
| `Tooling` | zero or one | Analyze, Format, Validate, and Custom Action selections |
| `Stage` | zero or one | Project-owned deployment files/directories |
| `Launch` | zero or more | Named process launch definitions |
| `Testing` | zero or one | Test-context dependencies and execution policy for this product |
| `Publish` | zero or more | Named folder/archive/installer outputs |
| `Refinements` | zero or one | Selection-specific assignments permitted by category merge laws |

Product validity:

| Type | Build | Launch | Testing | Publish | Special rule |
| --- | --- | --- | --- | --- | --- |
| Application | yes | yes | yes | yes | Produces one executable |
| Library | yes | no | yes | yes | Produces a static, shared, or interface library |
| Tool | yes | yes | yes | yes | Produces a host/developer executable |
| Test | yes | yes | no | optional | Its root dependencies are test-context dependencies |
| Benchmark | yes | yes | no | optional | Its root dependencies are benchmark-context dependencies |
| Plugin | yes | no | yes | yes | Produces a dynamically loaded artifact; loading is runtime-owned |
| External | no | optional | optional | yes | Build artifact is supplied by registered integration metadata |

`Build` may be omitted when conventions supply all required inputs. Invalid
sections are semantic-validation errors even if structurally valid XML.

### Metadata

```xml
<Metadata>
  <Description>Example application</Description>
  <License>Apache-2.0</License>
  <Homepage>https://example.invalid</Homepage>
  <Vendor>Example</Vendor>
</Metadata>
```

Each child is optional and appears at most once. License values use SPDX
expressions where possible.

### Typed Options

Project and package option declarations share one vocabulary:

```xml
<Options>
  <Boolean Name="Reflection" Default="false" Artifact="true" />
  <Enum Name="Distribution" Default="Bundled" Artifact="true">
    <Value Name="Bundled" />
    <Value Name="System" />
  </Enum>
  <String Name="Channel" Default="development" Artifact="false" />
  <Integer Name="WorkerCount" Default="4" Min="1" Max="64" Artifact="false" />
  <Path Name="ShaderDirectory" Default="shaders" Artifact="false" />
</Options>
```

| Declaration | Value rule |
| --- | --- |
| `Boolean` | `true` or `false` |
| `Enum` | exactly one declared `Value` name |
| `String` | UTF-8 text; optional allowed-value validation may be added through `Value` children |
| `Integer` | signed 64-bit integer within optional `Min`/`Max` |
| `Path` | normalized path under the field's declared path base |

`Artifact="true"` means changing the option may produce/select a different
PackageInstance artifact and therefore participates in binary compatibility and
dependency locking. It does not mean every assignment necessarily changes an
artifact; the package integration binding determines the final input set.

Assignments use:

```xml
<Option Name="Distribution" Value="Bundled" />
```

An assignment to an undeclared option, a type mismatch, or two different values
at equal authority is an error.

### Dependencies and export activation

```xml
<Dependencies>
  <Package Name="fmt" Exact="11.0.2" />

  <Package Name="OpenSSL" Compatible="3">
    <Use Library="TLS" />
    <Option Name="Linkage" Value="Static" />
  </Package>

  <Project Name="Game.Engine" Path="../Game.Engine/Game.Engine.nginproj" />
</Dependencies>
```

`Package` requires `Name` and exactly one version source:

- `Exact="x.y.z"`;
- `Compatible="x"`, `Compatible="x.y"`, or a complete compatible version;
- one child `Version`;
- no version attribute/child when the workspace supplies a central constraint.

Advanced constraints avoid XML comparison entities:

```xml
<Package Name="OpenSSL">
  <Version AtLeast="3.2.0" Before="4.0.0" />
  <Use Library="TLS" />
</Package>
```

`Version` accepts `AtLeast`, `After`, `AtMost`, and `Before`; contradictory or
empty ranges are errors. Semantic-version ordering includes prerelease
identifiers. Build metadata does not affect precedence.

`Use` has exactly one kind attribute:

```xml
<Use Library="Core" />
<Use Tool="MetaGen" />
<Use Plugin="Telemetry" />
<Use Action="ReflectionCodegen" />
<Use Asset="ExampleFonts" />
```

Ordinary product code should activate Tool/Action exports through `Generate` or
`Tooling`, not a direct `Use`, unless it intentionally consumes the Tool as a
product artifact.

When `Use` is absent, the package's default export set activates. Zero default
exports, multiple conflicting defaults, or an unknown export are errors. All
export names are unique within one package regardless of export kind.

Projects declare libraries whose APIs their source uses directly even when a
transitive requirement also exposes them. Transitive public requirements still
propagate their usage requirements, but they do not create a direct-dependency
claim in diagnostics or authorship metadata.

There is no `Scope`, generic `Optional`, generic `Link`, or nested `Feature`.

### Build

```xml
<Build>
  <Language Standard="C++23" Extensions="false" Required="true" />
  <Source Include="src/**.cpp" />
  <Header Include="include/**.hpp" Visibility="Public" />
  <CxxModule Include="src/**.ixx" Kind="Interface" Visibility="Public" />
  <Resource Include="assets/**" Into="assets" />
  <IncludeDirectory Path="include" Visibility="Public" />
  <Define Name="GALLERY_BUILDING" Value="1" Visibility="Private" />
  <CompileOption Value="-Wall" Visibility="Private" />
  <LinkOption Value="-Wl,--as-needed" Visibility="Private" />
</Build>
```

Core build item kinds:

| Element | Stable identity | Important fields |
| --- | --- | --- |
| `Language` | language | `Standard`, `Extensions`, `Required` |
| `Source` | normalized matched path | item operation plus `Generated` |
| `Header` | normalized matched path | item operation plus `Visibility`, `Generated` |
| `CxxModule` | normalized matched path | item operation plus `Kind`, `Visibility` |
| `Resource` | source/destination tuple | item operation plus `Into` |
| `IncludeDirectory` | normalized path plus visibility | `Path`, `Visibility`, optional `System` |
| `Define` | name plus visibility | `Name`, optional `Value`, `Visibility` |
| `CompileOption` | value plus visibility | `Value`, `Visibility` |
| `LinkOption` | value plus visibility | `Value`, `Visibility` |
| `PrecompiledHeader` | normalized header plus visibility | `Path`, `Visibility` |
| `UnityBuild` | singleton | `Enabled`, optional `BatchSize` |

Visibility is `Private`, `Public`, or `Interface`; product type validation
rejects meaningless public/interface use.

Collection operations use one of:

```xml
<Source Include="src/**.cpp" Exclude="src/legacy/**" />
<Source Remove="src/platform/**.cpp" />
<Source Update="src/generated.cpp" Generated="true" />
```

Exactly one of `Include`, `Remove`, or `Update` is required. `Exclude` is valid
only with `Include`. Remove and Update must match an existing item after lower
authority contributions; an ineffective operation is an error unless it says
`AllowEmpty="true"`.

Conventions have stable identities and behave like the lowest-authority
Includes. The initial conventions are:

- `NGIN.Cxx.Sources`: `src/**.c`, `src/**.cc`, `src/**.cpp`, `src/**.cxx`;
- `NGIN.Cxx.Headers`: `include/**.h`, `include/**.hh`, `include/**.hpp`,
  `include/**.hxx`;
- `NGIN.Cxx.Modules`: `src/**.ixx`, `src/**.cppm`;
- generated/build directories and version-control metadata are excluded.

`<Build Conventions="false">` disables all project file conventions. A named
convention can be disabled with `<Convention Name="NGIN.Cxx.Sources"
Enabled="false" />` inside Build.

### Actions

```xml
<Generate Action="NGIN.Reflection.MetaGen::ReflectionCodegen">
  <Input Include="include/**.hpp" />
  <Option Name="Namespace" Value="Gallery" />
</Generate>

<Tooling>
  <Analyze Action="NGIN.Tooling.ClangTidy::Analyze" />
  <Format Action="NGIN.Tooling.ClangFormat::Format" />
</Tooling>
```

Project verbs must match the exported Action `Kind`. Selecting an Action
activates its package and referenced Tool export on a host PackageInstance. It
does not execute until the corresponding command derives and authorizes an
ActionPlan. Package presence never selects or executes an Action.

### Stage

```xml
<Stage>
  <File Include="config/app.cfg" Into="config/app.cfg" />
  <Directory Include="assets" Into="assets" />
</Stage>
```

Project Stage describes project-owned deployment inputs. Package Library,
Plugin, Tool, and Asset exports contribute required files automatically when
active. Required package/export contributions cannot be removed by a consumer.

Every staged destination has one owner. Identical-byte coalescing may be
allowed by explicit policy; otherwise collisions are errors. Destinations are
relative, normalized, and cannot escape the stage root.

Plugin activation stages the plugin artifact and required files. It does not
configure discovery, loading, services, modules, or runtime ordering.

### Launch

```xml
<Launch Name="Development" Default="true">
  <Executable Product="Gallery" />
  <WorkingDirectory Path="." />
  <Argument>--gallery</Argument>
  <Environment Name="LOG_LEVEL" Value="debug" />
</Launch>
```

Launch is process intent only. It may select a built executable or explicit
Tool export, arguments, working directory, environment, and process-level
prerequisites. It cannot configure application services or runtime modules.
Secret values refer to an external secret name; secret contents are never
stored in XML, graph serialization, locks, or launch descriptors.

### Testing

```xml
<Testing>
  <Dependencies>
    <Package Name="Catch2" Compatible="3" />
  </Dependencies>
  <Argument>--reporter</Argument>
  <Argument>console</Argument>
  <Timeout Seconds="60" />
</Testing>
```

Dependencies in Testing participate only in the TestPlan for a non-Test
product. A project whose primary Type is `Test` declares its test dependencies
in root `Dependencies` instead.

### Publish

```xml
<Publish Name="portable">
  <Archive Format="zip" Output="dist/${project.name}-${project.version}.zip" />
</Publish>
```

Core output kinds are `Folder`, `Archive`, and `Installer`. Initially supported
formats remain `zip`, `tgz`, `msi`, and `deb`. The graph records semantic output
intent; publisher-specific commands exist only in PublishPlan and its adapter.

### Refinements

```xml
<Refinements>
  <Refinement>
    <Select>
      <Configuration Name="Debug" />
      <Target OS="windows" Architecture="x64" />
      <Toolchain Name="msvc" />
      <Option Name="Distribution" Value="Bundled" />
    </Select>

    <Build>
      <Define Name="GALLERY_DIAGNOSTICS" Value="1" Visibility="Private" />
    </Build>
  </Refinement>
</Refinements>
```

All selectors in one `Select` are ANDed. There is no arbitrary expression,
negation, script, or reference to runtime environment. Specificity is the count
of explicitly constrained first-class facts plus option assignments. Two
matching refinements that write incompatible values at equal specificity are
errors; XML order is never a tie-breaker.

## Package manifest

### Root and sections

```xml
<Package Name="Example" Version="1.2.0">
</Package>
```

`Name` and exact semantic `Version` are required. `SchemaVersion` and generic
platform-version attributes are invalid.

| Section | Cardinality | Purpose |
| --- | --- | --- |
| `Metadata` | zero or one | Description, license, homepage, repository |
| `Options` | zero or one | Typed public package options |
| `Requires` | zero or one | Package-level requirements active with any export |
| `Contributions` | zero or one | Package-level required runtime files/notices |
| `Exports` | one | Named Library, Tool, Plugin, Action, and Asset exports |
| `Integrations` | zero or one | Registered backend namespace elements |
| `Compatibility` | zero or one | Supported Target and Toolchain facts |

### Requirements

`Requires` accepts `Package`, `Project` only for local source composition,
`Capability`, and `Option` predicates. Package requirements use the same
version and `Use` syntax as project dependencies.

Package-level requirements apply when any export is active. Export-local
requirements appear under that export and apply only when it is active.

An option predicate is structured:

```xml
<When Option="Reflection" Equals="true">
  <Package Name="NGIN.Reflection" Compatible="0.4" />
</When>
```

`When` accepts exactly one declared Option and equality against a legal value.
Target predicates use `<When TargetOS="windows">`. Nested Boolean expressions,
arbitrary text evaluation, and scripts are invalid.

### Contributions

```xml
<Contributions>
  <Notices>
    <Notice Include="LICENSES/**" Into="notices/Example" />
  </Notices>
</Contributions>
```

Package-level contributions apply when any export is active. Notices and other
legal obligations normally belong here.

### Export rules

- Export names are unique package-wide.
- `Default="true"` makes an export part of the default set.
- An export may have local `Requires`, `Provides`, `RuntimeFiles`, and
  `Notices`.
- An export activates only on a concrete PackageInstance.
- Export requirement cycles are errors and report the complete path.
- Runtime composition concepts are invalid in every export.

#### Library

```xml
<Library Name="TLS">
  <Requires>
    <Export Library="Crypto" />
  </Requires>
  <Provides>
    <Capability Name="NGIN.Net.TLS" Version="1.0.0" />
  </Provides>
  <RuntimeFiles>
    <File Include="bin/**" Into="bin" />
  </RuntimeFiles>
</Library>
```

Library describes semantic headers/link artifacts and public/private
requirements. Concrete CMake target names are integration bindings, not Library
fields.

#### Tool

```xml
<Tool Name="MetaGen" />
```

Tool is an exported host-executable component. It is distinct from a package's
private build helper and from a target/runtime executable. An Action references
a Tool export by name.

#### Plugin

```xml
<Plugin Name="Telemetry">
  <RuntimeFiles>
    <File Include="bin/Telemetry.plugin" Into="plugins" />
  </RuntimeFiles>
</Plugin>
```

Plugin is a dynamically loadable product/artifact. Its metadata stops at build,
compatibility, dependencies, and deployment. Loading entry points, discovery,
service registration, modules, and lifecycle are runtime-framework concerns.

#### Action

```xml
<Action Name="ReflectionCodegen" Kind="Generate" Tool="MetaGen">
  <Inputs>
    <Header Include="include/**.hpp" />
  </Inputs>
  <Outputs>
    <Source Path="generated/reflection.cpp" />
  </Outputs>
</Action>
```

Kinds are `Generate`, `Analyze`, `Format`, `Validate`, and `Custom`. All Actions
reference a Tool export. Inputs, outputs, arguments, working-directory needs,
environment requirements, determinism, and generated build-item contribution
are declared. Undeclared outputs and output collisions are errors.

#### Asset

```xml
<Asset Name="ExampleFonts" Description="Fonts used by example themes">
  <File Include="assets/fonts/**" Into="fonts" />
</Asset>
```

Assets are optional named file collections. Data required by a Library or
Plugin belongs to that export's RuntimeFiles instead.

### Capabilities

Every capability implementation has an exact semantic Version:

```xml
<Provides>
  <Capability Name="NGIN.Net.TLS" Version="1.0.0" />
</Provides>
```

Requirements use `Exact`, `Compatible`, or structured `Version` constraints.
All active constraints intersect. Resolution creates a CapabilityBinding from
the requirement to one compatible active implementation export. Ambiguity and
exclusive conflicts are errors.

Capabilities may represent only acquisition, compilation, linking, generation,
artifact selection, or deployment behavior. Runtime services, DI registrations,
application modules, and lifecycle capabilities are invalid.

### Compatibility

```xml
<Compatibility>
  <Target OS="windows" Architecture="x64" />
  <Target OS="linux" />
  <Toolchain Compiler="msvc" />
</Compatibility>
```

Entries are allowed sets; absent categories are unconstrained. Binary package
compatibility is derived from selected Target, Toolchain, Configuration,
linkage, and artifact-affecting Options. ABI is not authored on Target.

## Workspace manifest

### Root and sections

```xml
<Workspace Name="NGIN">
</Workspace>
```

| Section | Cardinality | Purpose |
| --- | --- | --- |
| `Projects` | one | Explicit/glob project discovery |
| `Configurations` | zero or one | Named build configurations |
| `Targets` | zero or one | Structured target aliases |
| `Toolchains` | zero or one | Compiler/linker/runtime definitions |
| `Defaults` | zero or one | Output and selection defaults |
| `Packages` | zero or one | Sources, local packages, central constraints, bindings |
| `Policies` | zero or one | Trust, compatibility, reproducibility, staging policy |
| `Presets` | zero or one | Pre-resolution command/selection aliases |

There are no Workspace Profiles or product-kind overlays.

### Discovery

```xml
<Projects>
  <Project Path="Examples/Hello/Hello.nginproj" />
  <Project Include="Tools/**.nginproj" Exclude="Tools/Legacy/**" />
</Projects>
```

Duplicate normalized project paths and two different projects with the same
workspace product identity are errors.

### Selection declarations

```xml
<Configurations>
  <Configuration Name="Debug">
    <Optimization Mode="Off" />
    <DebugSymbols Enabled="true" />
  </Configuration>
  <Configuration Name="Release">
    <Optimization Mode="Speed" />
    <DebugSymbols Enabled="false" />
  </Configuration>
</Configurations>

<Targets>
  <Target Name="win-x64" OS="windows" Architecture="x64" />
  <Target Name="linux-x64" OS="linux" Architecture="x64" />
</Targets>

<Toolchains>
  <Toolchain Name="msvc"
             Compiler="msvc"
             RuntimeLibrary="dynamic"
             Linker="link" />
</Toolchains>
```

Target aliases resolve to structured facts; alias spelling is not graph
identity. Toolchain owns ABI/runtime facts. The implementation may discover
versions, but discovered values become explicit resolved facts and provenance.

### Defaults

```xml
<Defaults>
  <OutputRoot Path="build/ngin" />
  <Configuration Name="Debug" />
  <Target Name="host" />
  <Toolchain Name="default" />
</Defaults>
```

Workspace defaults are lower authority than project assignments and
refinements. Policy is not a default and cannot be overridden.

### Packages

```xml
<Packages>
  <Source Name="local" Kind="Directory" Path="Packages" />
  <LocalPackage Name="NGIN.Base"
                Manifest="Packages/NGIN.Base/NGIN.Base.nginpkg"
                Root="Dependencies/NGIN/NGIN.Base" />
  <Version Name="NGIN.Base" Compatible="0.4" />
  <Binding Package="OpenSSL" Source="conan" Coordinate="openssl" />
</Packages>
```

`Source` declares a PackageProvider configuration. This overhaul implements the
existing local/directory and current CMake-facing external provider needs; new
provider families are outside scope. Provider-native identity remains in the
PackageProviderResult and dependency lock.

Central Version constraints combine with project/package requirements by
intersection. Unused central entries, unbound provider coordinates, ambiguous
sources, and conflicting local packages are diagnostics.

### Policies

Policies are gates, never override values. Initial policy areas are:

- allowed PackageProviders and source roots;
- locked/integrity-required CI behavior;
- trusted Action executable origins and signatures;
- symlink/path boundary behavior;
- stage collision behavior;
- allowed Target/Toolchain combinations;
- non-hermetic package-result allowance.

### Presets

```xml
<Presets>
  <Preset Name="dev" Command="build">
    <Configuration Name="Debug" />
    <Target Name="host" />
    <Toolchain Name="default" />
    <Option Name="Distribution" Value="Bundled" />
    <Launch Name="Development" />
  </Preset>
</Presets>
```

Preset expansion occurs before semantic resolution. Expanding a preset and
passing the same values explicitly produce identical graphs and composition
fingerprints.

## Semantic merge laws

| Category | Identity | Law | Conflict behavior |
| --- | --- | --- | --- |
| Scalar project setting | field | convention, workspace default, project, refinement, allowed CLI | incompatible equal-authority assignment is error |
| Version constraint | package/capability requirement | intersection | empty intersection is error with all sources |
| Required dependency | PackageCoordinate request plus context | set union then closure | incompatible instance/coexistence is error |
| Package/export contribution | normalized owner/destination | accumulate | destination collision is error |
| Package Option | package instance plus option name | declared authority chain | equal-authority mismatch is error |
| Project Option | product plus option name | declared authority chain | equal-authority mismatch is error |
| Workspace policy | policy key | logical gate/intersection | violation is error; project cannot override |
| Build item | kind plus stable item identity | Include, Exclude, Remove, Update | duplicate/incompatible Update is error |
| Capability requirement | capability name plus requester context | accumulate and intersect Version | no/ambiguous implementation is error |
| Capability implementation | active export plus capability name/version | accumulate | exclusivity violation is error |
| Export activation | PackageInstance plus export name | set union then requirement closure | unknown/conflicting export is error |
| Action selection | qualified Action plus invocation identity | set union | incompatible duplicate invocation is error |
| Preset | not semantic | expand before resolution | conflicting explicit input is command error |
| Dependency lock | PackageInstance acquisition identity | constrains provider resolution | incompatible authored constraint is error |

## Package identity and reproducibility

```text
PackageCoordinate
  name
  exact package version

PackageProviderResult
  provider kind and coordinate/revision
  source/install/archive location
  integrity
  host or target context
  compatibility inputs
  provenance and trust

PackageInstance
  PackageCoordinate
  PackageProviderResult identity
  context
  derived BinaryCompatibility
  artifact-affecting Options
```

Exports activate on PackageInstances. Multiple instances are legal only when
their contexts or artifacts are genuinely distinct and their final linkage
closures permit coexistence.

The dependency lock stores exact acquired PackageInstances and only selection
facts that affect acquisition, closure, or artifacts. The canonical semantic
graph produces a separate composition fingerprint covering selection, active
exports, capability bindings, Actions, Tools, Plugins, edges, and
contributions. Backend execution plans may have additional fingerprints.

## Paths and globs

- A relative authored path is relative to the manifest containing it.
- A field that permits workspace-relative paths uses `Base="Workspace"`.
- Absolute paths are invalid in portable authored manifests unless a field is
  explicitly documented as a local machine override.
- `/` is the canonical authored and serialized separator. `\` is rejected in
  authored portable paths rather than interpreted differently by host.
- `.` is removed during normalization. `..` is allowed only when the normalized
  result remains under the declared owner/workspace boundary.
- Symlinks are resolved for boundary checks. A symlink may not escape the
  allowed root unless workspace policy explicitly permits that exact root.
- Symlink cycles are errors.
- `*` matches within one path segment; `?` matches one non-separator character;
  `[abc]` and ranges are supported within one segment; `**` is a complete
  segment matching zero or more directories.
- Matches are serialized in Unicode code-point order by normalized `/` path.
- Target filesystems that are case-insensitive reject case-fold collisions even
  when the authoring host is case-sensitive.
- Stage destinations are relative and may never normalize outside stage root.
- Canonical graph/lock identity uses portable owner-relative paths. Machine
  absolute paths may appear only in explicitly non-canonical diagnostic fields.

## Placeholders

Placeholders use `${name}` and are non-recursive. Initial names are:

| Placeholder | Type | Valid phases |
| --- | --- | --- |
| `${project.name}` | identifier | output/stage/launch/publish |
| `${project.version}` | semantic version | output/publish |
| `${configuration}` | identifier | output/stage/launch/publish |
| `${target.os}` | identifier | output/stage/launch/publish |
| `${target.architecture}` | identifier | output/stage/launch/publish |
| `${output.name}` | filename | stage/launch/publish |
| `${workspace.root}` | path | local execution only; excluded from canonical identity |

Unknown placeholders, recursive expansion, phase-invalid use, and runtime values
during build resolution are errors. Expansion is typed before escaping or path
normalization.

## Runtime boundary

Manifests never declare:

- application hosts;
- dependency-injection registrations;
- runtime services or service requirements;
- application/runtime modules;
- startup stages or module ordering;
- runtime backend selection performed by application code;
- plugin discovery/load/unload policy.

NGIN.Core and other frameworks may expose their own C++ configuration APIs and
runtime graph inspection. That data is not part of `.nginproj`, `.nginpkg`, the
CLI Composition Graph, or manifest capabilities.

## Validation and diagnostics

One internal ManifestSpec drives:

- structural XSD;
- parser structure and source locations;
- editor completion/hover metadata;
- semantic validator registration;
- vocabulary/reference generation checks.

XSD validates structure, namespaces, cardinality, and simple types. CLI
semantic validation additionally checks product rules, cross references, merge
conflicts, constraints, activation closure, compatibility, PackageProviders,
trust, paths, and runtime-boundary violations.

Diagnostics have stable codes, severity, manifest path, line/column, primary
message, contributing source locations, and a fix hint only when one correction
is unambiguous.

## Graph and plan boundary

The immutable Composition Graph contains products, concrete selection facts,
PackageInstances, active exports, semantic edges, resolved Options,
CapabilityBindings, Actions/Tools, deployable Plugins/artifacts,
contributions, and provenance.

It never contains CMake targets/cache values, opaque backend data, command
lines, temporary paths, copy work queues, installer commands, application
services, or runtime modules.

Resolution also returns immutable backend IntegrationBindings. Deterministic
RestorePlan, BuildPlan, ActionPlan, StagePlan, LaunchPlan, TestPlan, and
PublishPlan objects are derived from the graph plus the bindings/policy they
need. No command rereads authored XML after graph construction.
