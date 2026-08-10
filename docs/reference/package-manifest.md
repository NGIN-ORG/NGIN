# Package manifest reference

A `.nginpkg` describes one exact package release, its semantic requirements,
and its named Exports. Backend integration is optional extension metadata; it
does not define package meaning.

```xml
<Package xmlns:cmake="urn:ngin:integration:cmake"
         Name="Math"
         Version="1.0.0">
  <Exports>
    <Library Name="Math" Default="true" />
  </Exports>

  <Integrations>
    <cmake:AddSubdirectory Source=".">
      <cmake:Target Export="Math" Name="Math::Math" />
    </cmake:AddSubdirectory>
  </Integrations>
</Package>
```

`Name` and `Version` form the logical PackageCoordinate. The pre-release
grammar has no public format number.

## Package shape

| Section | Purpose |
| --- | --- |
| `Metadata` | Human-facing description, license, homepage, and repository facts |
| `Options` | Typed public package choices |
| `Requires` | Package, project, Export, Capability, and Option requirements |
| `Contributions` | Package-level notices, runtime files, and assets |
| `Exports` | Named Libraries, Tools, Plugins, Actions, and Assets |
| `Integrations` | Registered backend extension metadata outside package semantics |
| `Compatibility` | Target/toolchain compatibility and coexistence policy |

At least one Export is required. A package with several Exports is one acquired
package release, not several packages.

## Exports and activation

Export types are explicit:

```xml
<Exports>
  <Library Name="Core" Default="true" />
  <Library Name="TLS">
    <Requires>
      <Export Library="Core" Visibility="Public" />
    </Requires>
  </Library>
  <Tool Name="Codegen" />
  <Plugin Name="Telemetry" />
  <Asset Name="Fonts" />
</Exports>
```

The default Export set activates only when a project requests the package
without explicit `<Use>` children. Explicit use selects named Exports:

```xml
<Package Name="Example.Security" Compatible="3">
  <Use Library="TLS" />
</Package>
```

Activation follows typed Export requirements to a fixed point. It does not
activate every Export in the package. `Public` and `Private` describe dependency
propagation; they are not encoded scope strings.

## Requirements

Package requirements use readable Version forms that need no escaped comparison
operators:

```xml
<Requires>
  <Package Name="Example.Base" Visibility="Public">
    <Version AtLeast="2.1.0" Before="3.0.0" />
    <Use Library="Core" />
  </Package>

  <Capability Name="Example.TLS"
              Domain="Link"
              Compatible="1" />
</Requires>
```

Supported requirement categories are:

- Package requirements with exact, compatible, or structured Version
  constraints;
- project requirements where workspace composition provides another product;
- local Export requirements within the same package;
- versioned Capability requirements in an explicit semantic domain; and
- restricted Option predicates.

Requirement conditions may inspect declared Options and structured Target or
Toolchain facts. Packages cannot define arbitrary expression languages,
profiles, or runtime environments.

## Options

Options are typed and have one documented merge authority:

```xml
<Options>
  <Boolean Name="Reflection" Default="false" Artifact="true" />
  <Enum Name="Allocator" Default="System" Artifact="true">
    <Value Name="System" />
    <Value Name="Mimalloc" />
  </Enum>
  <String Name="Namespace" Default="example" />
  <Integer Name="ShardCount" Default="1" Minimum="1" Maximum="64" />
  <Path Name="SchemaRoot" Default="schemas" />
</Options>
```

`Artifact="true"` means the resolved value participates in PackageInstance
identity. A backend extension may map a semantic Option to native inputs, but it
must agree with this artifact declaration.

Options replace the old generic Feature mechanism only when the concept is
genuinely a user choice. Libraries, Tools, Plugins, Actions, Assets,
Capabilities, and required contributions remain their own types.

## Capabilities

An Export can provide a versioned build/package/deployment Capability:

```xml
<Library Name="TLS">
  <Provides>
    <Capability Name="Example.TLS"
                Domain="Link"
                Version="1.2.0" />
  </Provides>
</Library>
```

Resolution binds each Capability requirement to exactly one compatible
implementation Export. No implementation and multiple compatible
implementations are both errors. Capabilities do not model runtime services,
modules, dependency injection, or application lifecycle.

## Tools and Actions

Tools are host-executable Exports. Actions are declarative contracts that name a
Tool; they do not execute merely because the package is present.

```xml
<Exports>
  <Tool Name="MetaGen" />
  <Action Name="Generate"
          Kind="Generate"
          Tool="MetaGen"
          Deterministic="true">
    <Outputs>
      <Source Path="generated/reflection.cpp" />
    </Outputs>
  </Action>
</Exports>
```

An explicit project verb selects the Action. Resolution creates a distinct host
PackageInstance, activates the Action and its Tool, applies workspace trust
policy, and derives an ActionPlan. Action output paths resolve under the
ActionPlan output root.

## Contributions

Package- or Export-level contributions may contain notices, runtime files or
directories, and asset files or directories. They become active only when their
owner is active and retain source provenance in the Composition Graph.

```xml
<Plugin Name="Telemetry">
  <RuntimeFiles>
    <File Include="plugins/telemetry.*" Into="plugins" />
  </RuntimeFiles>
</Plugin>
```

A Plugin describes a deployable dynamically loadable artifact. It does not tell
an application framework to load, configure, or order the Plugin.

## Compatibility and coexistence

```xml
<Compatibility Coexistence="Context">
  <Target OS="windows" Architecture="x64" />
  <Toolchain Compiler="msvc" />
</Compatibility>
```

`Context` allows distinct host and target instances but rejects incompatible
instances in one linkage closure. `SideBySide` additionally requires explicit
platform support; declaring it does not make an ABI or loader capable of
side-by-side use.

## CMake integration

CMake-specific source modes, cache variables, discovery names, and target
mappings live under the registered CMake namespace:

```xml
<Integrations>
  <cmake:FindPackage Name="OpenSSL" Required="true">
    <cmake:Target Export="Crypto" Name="OpenSSL::Crypto" />
    <cmake:Target Export="TLS" Name="OpenSSL::SSL" />
  </cmake:FindPackage>
</Integrations>
```

The implemented modes are `cmake:AddSubdirectory`, `cmake:Isolated`,
`cmake:FindPackage`, and `cmake:Manual`. They resolve into immutable
`CMakeIntegrationBindings` outside the Composition Graph. See
[CMake integration](cmake-integration.md) for their exact contracts.

## PackageProviders and identity

A PackageProvider resolves a logical request to an exact
PackageProviderResult. The resolver combines that result with host/target
context, derived BinaryCompatibility, and artifact-affecting Options to form a
PackageInstance. Named Exports activate on that PackageInstance.

PackageProvider-native revision, integrity, provenance, trust, and acquisition
roots are not backend cache variables. The dependency lock records exact
acquired instances; the separate composition fingerprint records active
semantic choices.
