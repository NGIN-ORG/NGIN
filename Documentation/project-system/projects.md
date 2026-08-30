---
title: Projects
description: Describe one executable or library with a product-first .nginproj manifest.
---

# Projects

One `.nginproj` describes one physical product. The root element determines the
product kind.

## Executable

```xml
<Executable Name="Tool">
  <Build>
    <Source Include="src/**/*.cpp" />
    <IncludeDirectory Include="include" Visibility="Private" />
  </Build>
</Executable>
```

An executable has implicit default Run intent. It may also declare named Run,
Test, and Benchmark registrations.

## Library

```xml
<Library Name="Geometry" Kind="Static">
  <Build>
    <PublicHeader Include="include/**/*.hpp" />
    <Source Include="src/**/*.cpp" />
    <IncludeDirectory Include="include" Visibility="Public" />
  </Build>
</Library>
```

Supported library kinds are `Static`, `Shared`, `Interface`, and `Plugin`.
Visibility matters because public requirements flow to consumers while private
requirements remain implementation details.

## Semantic sections

Project behavior belongs in direct product sections:

| Section | Purpose |
| --- | --- |
| `Build` | Sources, headers, includes, definitions, features, and links |
| `Stage` | Runtime files, libraries, assets, and layout |
| `Run` | Executable arguments, environment, and working directory |
| `Test` | Registered test intent |
| `Benchmark` | Registered benchmark intent |

## Validate the authored contract

```bash
ngin validate --project Tool.nginproj --configuration Debug
ngin schema --format json
```

The CLI schema is the executable syntax authority. Avoid copying older manifest
shapes from historical plans or generated build trees.
