---
title: Package manifest reference
description: Typed exports, capabilities, actions, CPS overlays, and backend adapters in .nginpkg manifests.
---

# Package manifest reference

A `.nginpkg` describes package semantics not already supplied by a published
NGIN product or portable CPS metadata. Typed exports are direct children of
`Package`; there is no `Exports` wrapper.

```xml
<Package Name="Example.Security" Version="3.2.1" CompatibleSince="3.0.0">
  <Library Name="Crypto" Default="true">
    <Provides Name="NGIN.Crypto" Version="1" />
  </Library>
  <Library Name="TLS">
    <Uses><Library Name="Crypto" Public="true" /></Uses>
    <Provides Name="NGIN.Net.TLS" Version="1" />
    <RuntimeFiles>
      <File From="bin/tls-runtime.*" To="bin/" />
    </RuntimeFiles>
  </Library>
</Package>
```

## Typed exports

Direct exports are Library, Tool, Plugin, Generator, Analyzer, Formatter,
Validator, Action, and Asset. One export is implicitly the default; packages
with several exports mark their default set explicitly.

Tool is a host-execution role. Plugin is a staged native role backed by a
Plugin library or CPS module. Selecting a plugin does not load it into an
application process.

## Capabilities and options

Exports provide versioned semantic capabilities with `Provides`. Projects and
packages may require a capability; exactly one compatible provider must remain
after constraints and workspace preference.

Boolean and Enum options affect artifact identity by default. Mark an option
non-artifact only when it cannot change acquired or built bytes.

## Actions

Generators, analyzers, formatters, and validators lower to a normalized action
model. Definitions are inert until selected by the corresponding project verb.

## CPS overlays

CPS is preferred for portable compiled components and usage requirements. An
NGIN wrapper can import CPS and add NGIN-specific capabilities, actions, assets,
notices, and staging semantics without duplicating compiled metadata.

## Adapters

Backend-specific bindings live under strongly validated adapter namespaces,
such as a CMake add-subdirectory adapter. Runtime service registration, plugin
loading, dependency injection, and application lifecycle do not belong in a
package manifest.
