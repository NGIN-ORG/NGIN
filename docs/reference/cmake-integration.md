# CMake integration extension

Status: Implemented and normative for the initial backend

NGIN project/package semantics are backend-neutral. CMake integration is
authored in a registered XML namespace and resolves to immutable
`CMakeIntegrationBindings` outside the Composition Graph.

This overhaul implements CMake only. An unregistered namespace or a request for
another backend is an error; NGIN does not approximate it through CMake.

## Namespace

```xml
<Package xmlns:cmake="urn:ngin:adapter:cmake"
         Name="Example"
         Version="1.0.0">
</Package>
```

The namespace has no public manifest-format number before the first official
release. Its structure is part of the generated structural schemas and
ManifestSpec metadata.

## Boundary

```text
semantic Package manifest
  Library/Tool/Plugin/Action/Asset exports
                       |
                       v
CMake extension metadata
  maps semantic identities to CMake integration
                       |
                       v
CMakeIntegrationBindings
  immutable sidecar outside Composition Graph
                       |
Composition Graph -----+
                       v
BuildPlan
                       |
                       v
CMake generation/configure/build
```

CMake target names, cache variables, generator names, toolchain files,
`find_package` details, source build directories, and CMake commands never enter
semantic export fields or graph serialization.

## Integration elements

One package may declare one applicable CMake adapter for a selected
PackageInstance. Platform additions use the same shallow typed `When` model as
core authoring and never select a winner by order.

### AddSubdirectory

```xml
<Adapters>
  <cmake:AddSubdirectory Source=".">
    <cmake:Cache Name="BUILD_TESTING" Value="OFF" />
    <cmake:Target Export="Library" Name="Example::Example" />
  </cmake:AddSubdirectory>
</Adapters>
```

`Source` is relative to the package provider root and must contain a
`CMakeLists.txt`. The adapter adds the source once per compatible
PackageInstance/build tree using an isolated binary directory. Duplicate CMake
target production and source reuse with incompatible cache inputs are errors.
Package integrations are emitted in dependency-first topological order so a
dependent package's standalone fallback cannot preempt an explicitly resolved
dependency integration.

### Isolated

```xml
<Adapters>
  <cmake:Isolated Source=".">
    <cmake:Cache Name="BUILD_TESTING" Value="OFF" />
    <cmake:Install />
    <cmake:FindPackage Name="Example" Config="true">
      <cmake:Target Export="Library" Name="Example::Example" />
    </cmake:FindPackage>
  </cmake:Isolated>
</Adapters>
```

Isolated configures/builds/installs into an NGIN-controlled prefix before the
consumer build. It is preferred when embedding would leak global CMake state or
when one source tree must produce several PackageInstances.

The derived BuildPlan records this as an install-before-use package step. The
consumer CMake project receives only the resulting prefix and `find_package`
binding; it does not attempt to run an ExternalProject during the same configure
that needs the installed package.

The PackageProviderResult and dependency lock record source integrity. The
installed artifact identity and artifact-affecting cache inputs participate in
PackageInstance identity.

### FindPackage

```xml
<Adapters>
  <cmake:FindPackage Name="OpenSSL" Config="false" Required="true">
    <cmake:Target Export="Crypto" Name="OpenSSL::Crypto" />
    <cmake:Target Export="TLS" Name="OpenSSL::SSL" />
  </cmake:FindPackage>
</Adapters>
```

| Attribute | Meaning |
| --- | --- |
| `Name` | CMake package name |
| `Config` | `true` for CONFIG-only, `false` for MODULE/default find behavior |
| `Required` | configure must fail when not found; defaults to `true` |
| `Version` | optional exact/provider-resolved CMake version passed to discovery |

The semantic NGIN version constraint is resolved before CMake. A CMake result
whose discovered version conflicts with the PackageInstance is an error.

### Manual

```xml
<Adapters>
  <cmake:Manual Source="cmake-wrapper">
    <cmake:Target Export="Library" Name="Example::Example" />
  </cmake:Manual>
</Adapters>
```

Manual integrates a package-wrapper-owned `CMakeLists.txt`. It is not an escape
for arbitrary shell commands and remains subject to declared export mappings,
provider roots, trust policy, and capability checks.

## Semantic export mappings

Every mapped `Export` names one semantic export in the containing package.
Unknown, duplicate, missing-required, or kind-incompatible mappings are errors.

```xml
<cmake:Target Export="Core" Name="NGIN::Core" />
<cmake:Target Export="MetaGen" Name="NGIN.Reflection.MetaGen" />
<cmake:Target Export="Telemetry" Name="Telemetry.Plugin" />
```

Expected CMake target kind:

| Semantic export | CMake expectation |
| --- | --- |
| Library | linkable or interface library target |
| Tool | executable target available for host execution |
| Plugin | module/shared library target or explicit deployed artifact binding |
| Action | no direct target; references its mapped Tool |
| Asset | no target; semantic file contributions provide it |

A target mapping is stored only in CMakeIntegrationBindings. Graph output shows
the semantic export and artifact facts, not the CMake target spelling.

## Cache inputs

Literal CMake cache inputs are backend-private:

```xml
<cmake:Cache Name="BUILD_SHARED_LIBS"
             Value="OFF"
             Type="BOOL"
             Artifact="true" />
```

`Type` is `BOOL`, `STRING`, `PATH`, or `FILEPATH`. `Artifact="true"` declares
that the value contributes to artifact identity.

Public semantic package Options map explicitly:

```xml
<cmake:MapOption Option="Reflection"
                 Cache="NGIN_CORE_FEATURE_REFLECTION"
                 True="ON"
                 False="OFF"
                 Artifact="true" />
```

Enum/string/integer/path mappings use `Value="${option.value}"`; this is a
CMake-extension typed placeholder, not a core recursive textual macro. Unknown
options and mappings that disagree with the semantic Option's `Artifact`
contract are errors.

## Selection

```xml
<cmake:When OS="windows" Architecture="x64" Compiler="msvc">
  <cmake:Cache Name="EXAMPLE_WINDOWS_BACKEND" Value="ON" />
</cmake:When>
```

Selection may inspect core Configuration, Target, Toolchain, and declared
Option facts. It cannot introduce another semantic dimension, runtime
environment, profile, arbitrary expression, or XML-order precedence.

Every matching block contributes additively. Applicable blocks that produce
incompatible keyed bindings or cache inputs are errors; there is no priority or
specificity winner.

## Generated project bindings

For an NGIN-authored project, the generated CMake adapter binds the semantic
product to a generated CMake target. Project XML does not opt into CMake or name
that target. Workspace/backend invocation selects the installed CMake adapter;
only CMake is available initially.

The adapter must support the semantic facts used by the graph or fail before
generation. Initial capability checks cover:

- executable, static, shared, interface, and plugin products;
- sources, headers, C++ module items where the selected CMake/toolchain supports
  them, resources, include directories, definitions, compile/link options;
- public/private/interface propagation;
- generated sources and Action dependencies;
- host Tool targets distinct from target artifacts;
- single- and multi-configuration generators;
- cross compilation inputs;
- staging and installed artifact discovery.

No unsupported fact is silently dropped.

## PackageProvider integration

PackageProviders acquire/locate packages and return normalized
PackageProviderResults. They do not inject cache values or CMake targets into
the Composition Graph.

Existing Conan/vcpkg behavior may prepare CMake discovery/toolchain inputs for
CMakeIntegrationBindings. Provider-native coordinates, revisions, triplets,
profiles, integrity, and hermeticity remain PackageProvider/dependency-lock
facts. Expanding provider feature support or adding provider families is outside
this overhaul.

## Plan and cache identity

BuildPlan includes selected CMakeIntegrationBindings, adapter version,
generator, single- or multi-configuration mode, cross-compilation state,
toolchain input, relevant cache inputs, and generated work paths. ActionPlan
contains selected Action identities, host Tool graph identities and CMake
targets, determinism, outputs, and provenance. Those facts produce distinct
deterministic plan identities and later cryptographic fingerprints.

They do not alter the canonical semantic Composition Graph or composition
fingerprint unless their semantic/artifact result changes. CMake-generated
temporary and absolute paths never enter canonical graph or dependency-lock
identity.

## Validation invariants

- Core graph types and serialization contain no CMake vocabulary.
- Every active semantic export needed by CMake has exactly one applicable
  binding or a portable resolved artifact sufficient to construct one.
- CMake mappings cannot activate semantic exports implicitly.
- Integration metadata cannot configure runtime services/modules/lifecycle.
- CMake cache inputs cannot create undeclared semantic Options.
- Host Tools never resolve to target-only executable targets.
- Package source paths remain under the normalized PackageProvider root.
- BuildPlan derivation uses the graph plus bindings and never rereads XML.
