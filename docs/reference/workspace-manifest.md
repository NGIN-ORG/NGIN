# Workspace manifest reference

A `.ngin` workspace owns discovery and shared selection policy. It does not
repeat project behavior.

```xml
<Workspace Name="Example">
  <Discover>
    <Projects Include="Examples/**/*.nginproj" />
    <Projects Include="Dependencies/NGIN/NGIN.Base" System="CMake" />
    <Packages Include="Packages/**/*.nginpkg" />
  </Discover>
  <Profiles Default="dev">
    <Profile Name="dev" Configuration="Debug" />
    <Profile Name="release" Configuration="Release" />
  </Profiles>
</Workspace>
```

## Discovery

Discover contains repeated Projects and Packages rules with `Include` and an
optional `Exclude`. An exact path is simply an Include without glob characters.
Normalized duplicate results are deduplicated with a diagnostic; results are
contained, sorted, and deterministic. A local package root is inferred from
its manifest directory.

`Projects` discovers developable **Workspace Projects**. A `.nginproj` file is
a native `Ngin` project. An explicitly named directory with a root
`CMakeLists.txt` is a `CMake` project. `System="Ngin|CMake"` is optional when
the exact candidate has only one valid interpretation. CMake discovery is
intentionally exact in version 1: globbing directories or recursively scanning
nested `CMakeLists.txt` files is rejected. A CMake project must use an authored
configure preset; NGIN never writes one on its behalf.

The built-in driver supports CMake 3.20 or newer and requests File API
codemodel v2, cmakeFiles v1, and toolchains v1. A project's
`cmakeMinimumRequired` may require a newer executable.

The CLI assigns each project a stable identity from the workspace manifest,
project system, and canonical project root. The same checkout may therefore
participate in two workspaces without sharing editor selection state.

## Built-ins and explicit selection

NGIN provides inspectable built-ins:

- Debug and Release configurations;
- target `host`;
- toolchain `auto`;
- output root `.ngin/build`;
- path containment, symlink rejection, stage collision errors, and action
  trust evaluation.

Workspaces declare additional Configurations, Targets, Toolchains, and typed
Options only when needed. Built-ins and overrides appear in
`ngin inspect --effective` with provenance.

## Versions

```xml
<Versions>
  <Package Name="Example.Core" Version="1.4" />
</Versions>
```

Central versions constrain requests but do not create dependencies. Unused
entries warn. Exact provider results belong in the dependency lock.

## Profiles

Profiles may select Configuration, Target, Toolchain, Run, and project/package
Option values. The Default profile supplies workspace defaults. A profile name
does not enter graph identity; only its expanded facts do.

## Capability preferences

```xml
<Capabilities>
  <Prefer Name="NGIN.UI.Backend" Provider="NGIN.UI.Backend.SDL3" />
  <When OS="windows">
    <Prefer Name="NGIN.Crypto" Provider="NGIN.Crypto.CNG" />
  </When>
</Capabilities>
```

Preferences filter compatible candidates and cannot make an incompatible
provider valid. Authored order never selects a winner.

## Trust

```xml
<Trust>
  <AllowActions From="Packages/NGIN.*" Reason="First-party workspace tools" />
</Trust>
```

Trust rules express explicit exceptions and lower into the strict policy model
with provider, integrity, locked-mode, signature, action-kind, and provenance
facts.
