# NGIN Typed Source Selectors Plan

Status: Implemented

## Summary

NGIN project manifests should make source ownership and simple build selection
first-class project authoring concepts.

The current model supports source discovery through `<SourceRoots>` and backend
include propagation through `<Build><IncludeDirectories>`. That works, but it
mixes two concerns:

- which files and folders belong to the project
- which include roots are public API versus private implementation detail

This plan introduces a typed `<Sources>` model for `.nginproj` files. It lets a
project declare public and private source sets directly, and lets individual
source entries opt into simple typed selectors such as operating system,
architecture, and build configuration.

Named conditions, expression languages, reusable condition aliases, and
`When="..."` indirection are intentionally left out of this plan. Those should
be discussed separately before they become part of the manifest contract.

## Goals

- Make public and private source ownership explicit in `.nginproj`.
- Avoid using backend-specific include directory declarations as the primary
  way to express public API surface.
- Keep common layouts simple, including `include/` plus `src/` and
  `Public/` plus `Private/`.
- Support direct typed selection for platform-specific or
  configuration-specific files.
- Preserve compatibility with existing `<SourceRoots>` manifests.
- Keep generated CMake behavior deterministic and easy to inspect.

## Non-Goals

- Do not introduce named `<Conditions>` yet.
- Do not introduce a condition expression language.
- Do not add arbitrary boolean expressions to source entries.
- Do not remove `<SourceRoots>` in the first implementation.
- Do not require projects to use a specific folder naming convention.
- Do not move package source ownership into `.nginpkg`.

## User-Facing Model

A library project can declare public headers and private implementation roots:

```xml
<Sources>
  <Public>
    <Root Path="include" />
  </Public>

  <Private>
    <Root Path="src" />
  </Private>
</Sources>
```

The same model supports Unreal-style folder names without making them the NGIN
default:

```xml
<Sources>
  <Public>
    <Root Path="Public" />
  </Public>

  <Private>
    <Root Path="Private" />
  </Private>
</Sources>
```

Specific files can be included explicitly:

```xml
<Sources>
  <Public>
    <Root Path="include" />
    <File Path="include/NGIN/Core/Kernel.hpp" />
  </Public>

  <Private>
    <Root Path="src" />
    <File Path="src/NGIN/Core/Kernel.cpp" />
  </Private>
</Sources>
```

For manually curated source lists, files can also be declared as a
line-separated block:

```xml
<Sources>
  <Private>
    <Files>
      src/a.cpp
      src/b.cpp
      src/c.cpp
    </Files>

    <Files OperatingSystem="windows">
      src/platform/WinMain.cpp
      src/platform/WindowsWindow.cpp
    </Files>
  </Private>
</Sources>
```

`<Files>` is authoring sugar for repeated `<File Path="..." />` entries. The
selector attributes on `<Files>` apply to every path in the block.

Typed selectors can be attached directly to roots or files:

```xml
<Sources>
  <Public>
    <Root Path="include" />
  </Public>

  <Private>
    <Root Path="src" />
    <Root Path="src/platform/windows" OperatingSystem="windows" />
    <Root Path="src/platform/linux" OperatingSystem="linux" />
    <File Path="src/platform/WinMain.cpp" OperatingSystem="windows" />
  </Private>
</Sources>
```

Roots can use simple include and exclude glob patterns to constrain recursive
discovery:

```xml
<Sources>
  <Private>
    <Root Path="src"
          Include="**/*.cpp;**/*.hpp"
          Exclude="**/*.generated.cpp" />
  </Private>
</Sources>
```

`Include` and `Exclude` patterns are relative to the root path and support `*`,
`?`, and `**`.

Build settings can use the same selector attributes:

```xml
<Build Backend="CMake"
       Mode="Generated"
       Language="CXX"
       LanguageStandard="23">
  <CompileDefinitions>
    <Definition Value="NGIN_CORE_BUILD"
                Visibility="Private" />
    <Definition Value="NGIN_PLATFORM_WINDOWS"
                Visibility="Private"
                OperatingSystem="windows" />
    <Definition Value="NGIN_DEBUG_TOOLS"
                Visibility="Private"
                BuildType="Debug" />
  </CompileDefinitions>
</Build>
```

## Selector Attributes

The first typed selector set should match values already present on project
configurations:

- `OperatingSystem`
- `Architecture`
- `BuildType`

An item with no selector attributes applies to every selected configuration.

An item with one or more selector attributes applies only when all provided
selectors match the active configuration. This is an AND model:

```xml
<File Path="src/platform/LinuxDebug.cpp"
      OperatingSystem="linux"
      BuildType="Debug" />
```

The first implementation should use exact string matching against the selected
configuration values after existing validation has normalized or accepted those
values. Wildcards, negation, OR groups, version ranges, and expression syntax
are deferred.

When a non-selected typed root or file is nested under a broader selected root,
generated source scanning excludes the non-selected path.

## Source Semantics

`<Sources><Public>` declares source roots and files that form the public API
surface of the project target.

Generated CMake should map public source roots to public include directories for
library targets. For executable targets, public source roots can still be used
as target include directories, but they do not create a consumer-facing API
because executables are not linked as libraries by normal project consumers.

`<Sources><Private>` declares source roots and files that are available only
while building the owning project target.

Generated CMake should map private source roots to private include directories.

Compilable source files discovered under selected roots should participate in
the generated target. Header files should be available for includes and tooling,
but should not become compiled translation units unless the backend explicitly
needs to list them for IDE visibility.

If the same file is selected through multiple source entries, tooling should
deduplicate the file by resolved path.

## Compatibility

Existing `<SourceRoots>` should remain valid.

For generated CMake, existing `<SourceRoots>` should keep its current behavior:

- roots are scanned for source files
- roots are added as private include directories

A project should not need to migrate immediately.

If a project uses both `<SourceRoots>` and `<Sources>`, the first implementation
should either:

- allow both and merge them, treating `<SourceRoots>` as private roots, or
- report a validation error asking the author to use one model

The recommended first implementation is to report a clear validation error. That
keeps the authored model unambiguous while compatibility remains available for
existing projects.

## Manifest Contract Changes

`docs/specs/002-project-and-target-manifest.md` has been updated to add
`Sources` as a supported root section.

The project root surface would become:

- `Sources`
- `SourceRoots`
- `Output`
- `Build`
- `References`
- `ConfigInputs`
- `Runtime`
- `Environments`
- `Profiles`

`SourceRoots` is documented as the legacy-compatible source declaration
surface. `Sources` is documented as the preferred source declaration
surface for new generated-mode projects.

The implemented source entry surface also includes `<Files>` batch entries and
root `Include` / `Exclude` glob attributes.

## Implementation Plan

1. Add manifest model types for typed source entries.
2. Parse root-level `<Sources>` in the CLI authoring loader.
3. Validate `Public` and `Private` groups and their child `Root` and `File`
   entries.
4. Add typed selector parsing for source entries and selected build settings.
5. Apply selector matching during generated project resolution.
6. Update generated CMake emission to map public roots to `PUBLIC` include
   directories and private roots to `PRIVATE` include directories.
7. Update source collection to include selected source roots and explicit files.
8. Add validation for manifests that mix `<SourceRoots>` and `<Sources>`.
9. Update spec documentation and README examples.
10. Add targeted CLI tests for parsing, validation, source collection, and CMake
    emission.

## Suggested Test Cases

- A project with only `<SourceRoots>` still builds with current behavior.
- A project with `<Sources><Private><Root Path="src" /></Private></Sources>`
  emits private include directories.
- A library project with `<Sources><Public><Root Path="include" /></Public>`
  emits public include directories.
- A selected `OperatingSystem="linux"` root is included for a linux
  configuration.
- A non-selected `OperatingSystem="windows"` root is omitted for a linux
  configuration.
- A `BuildType="Debug"` compile definition is emitted only for Debug.
- A manifest that mixes `<SourceRoots>` and `<Sources>` reports a clear error.
- Duplicate files selected through multiple entries are emitted once.

## Open Questions

- Should public source roots be allowed on executable projects, or should they
  be accepted but treated as private because executables have no link consumers?
- Should explicit public `File` entries be used only for IDE/tooling visibility,
  or should generated CMake list public headers in the target source list?
- Should selector values be case-sensitive to match the existing manifest style,
  or should they be normalized before matching?
- Should typed selectors also apply to `References`, `ConfigInputs`, and
  runtime entries later, or remain focused on build authoring?
