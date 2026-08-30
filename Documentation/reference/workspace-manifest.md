---
title: Workspace manifest reference
description: Discovery, built-ins, versions, profiles, capability preferences, and trust in .ngin workspaces.
---

# Workspace manifest reference

A `.ngin` workspace owns discovery and shared selection policy. It does not
repeat product behavior.

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

`Discover` contains repeated Projects and Packages rules. A `.nginproj` is a
native NGIN project. An explicitly named directory with a root `CMakeLists.txt`
is a CMake project. CMake directory discovery is exact rather than recursive.

## Built-ins

NGIN provides inspectable Debug and Release configurations, target `host`,
toolchain `auto`, output root `.ngin/build`, and strict path, symlink, collision,
and trust behavior.

## Versions

```xml
<Versions>
  <Package Name="Example.Core" Version="1.4" />
</Versions>
```

Central versions constrain requirements but do not create dependencies. Exact
resolved instances belong in the dependency lock.

## Profiles

Profiles select Configuration, Target, Toolchain, Run, and project or package
options. The profile label does not enter graph identity; its expanded facts do.

## Capability preferences

```xml
<Capabilities>
  <Prefer Name="NGIN.UI.Backend" Provider="NGIN.UI.Backend.SDL3" />
</Capabilities>
```

A preference filters compatible candidates. It cannot make an incompatible
provider valid.

## Trust

```xml
<Trust>
  <AllowActions From="Packages/NGIN.*"
                Reason="First-party workspace tools" />
</Trust>
```

Trust exceptions are explicit and retain provider, integrity, locked-mode,
signature, action-kind, reason, and provenance facts.
