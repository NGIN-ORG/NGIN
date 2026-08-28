# Package manifest reference

A `.nginpkg` describes package semantics that are not already supplied by a
published NGIN product or portable CPS metadata. Typed exports are direct
children of `Package`; there is no Exports wrapper.

```xml
<Package Name="Example.Security" Version="3.2.1" CompatibleSince="3.0.0">
  <Library Name="Crypto" Default="true">
    <Provides Name="NGIN.Crypto" Version="1" />
  </Library>
  <Library Name="TLS">
    <Uses><Library Name="Crypto" Public="true" /></Uses>
    <Provides Name="NGIN.Net.TLS" Version="1" />
    <RuntimeFiles><File From="bin/tls-runtime.*" To="bin/" /></RuntimeFiles>
  </Library>
</Package>
```

## Typed exports

Direct exports are Library, Tool, Plugin, Generator, Analyzer, Formatter,
Validator, Action, and Asset. One export is implicitly the default; packages
with several exports mark their default set explicitly. Local export
requirements are typed children of Uses and public propagation is explicit.

Tool is a consumption role backed by an executable product or component.
Plugin is a deployable role backed by a Plugin library or CPS module. Selecting
a Plugin activates and stages its artifact and contributions only.

## Capabilities and options

Exports provide versioned semantic capabilities with `Provides`. Package and
project Uses may require `Capability`; exactly one compatible implementation
must remain after provider constraints and workspace preferences.

Boolean and Enum options are declared under Options. Options affect artifact
identity by default; use `Artifact="false"` only for a semantic choice that does
not change the acquired or built artifact. Additive conditional package facts
use the same shallow typed When vocabulary as projects.

## Actions

Generator, Analyzer, Formatter, and Validator are meaningful authored action
roles that lower to the generic normalized action model. An action names its
backing Tool and declares typed Inputs and Outputs. Definitions are inert until
a project selects the corresponding verb.

## CPS overlays

CPS is the preferred contract for portable compiled components and usage
requirements:

```xml
<Package Name="OpenSSL" Version="3.5.2">
  <Import Cps="OpenSSL.cps" />
  <Capabilities>
    <Provide Name="NGIN.Crypto" Version="1" Component="OpenSSL:Crypto" />
    <Provide Name="NGIN.Net.TLS" Version="1" Component="OpenSSL:SSL" />
  </Capabilities>
</Package>
```

NGIN imports CPS executable, archive, dynamic-library, interface, and module
components. The supported compiled metadata is `location`, `includes`,
language-keyed `definitions` and `compile_flags`, `link_flags`, default
components, and component/package requirements. `@prefix@`, `cps_path`, and
non-relocatable `prefix` follow the [CPS schema](https://cps-org.github.io/cps/schema.html).
The overlay owns NGIN-specific capabilities, actions, assets, notices, and
staging semantics; it does not duplicate those imported CPS fields.

See the checked-in [Portable CPS overlay](../examples/project-model/Portable.nginpkg).

## Adapters

Backend-specific bindings live under Adapters using a strongly validated XML
namespace, for example `cmake:AddSubdirectory`. Adapter fields lower to an
immutable sidecar keyed by semantic identities and do not enter Composition
Graph identity. Prefer CPS when portable metadata exists.

Runtime application registration, dependency injection, plugin loading, and
module lifecycle never belong in package manifests.
