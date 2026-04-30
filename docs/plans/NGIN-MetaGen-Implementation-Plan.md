# NGIN.MetaGen Implementation Plan

> Superseded by Phase E of the V3 project model and the
> `NGIN.Reflection.MetaGen` package split: MetaGen is now a package-provided
> `ngin-metagen` command generator invoked through
> `NGIN.Reflection.MetaGen::ReflectionCodegen`. The standalone public
> `ngin metagen` command, CLI-linked MetaGen backend, and `<Build><MetaGen />`
> opt-in described below are obsolete historical notes.

## Summary

NGIN.MetaGen should become the opt-in code generation path for authored C++
metadata. The first implementation should focus on reflection metadata generated
from annotated C++ declarations, exposed through the existing `ngin` CLI as
`ngin metagen`.

MetaGen is not intended to be magic runtime RTTI discovery. It should be a
build-time tool that uses the real C++ compiler frontend to find explicit NGIN
annotations and emit normal typed C++ source files. `NGIN.Core` and plain
generated-mode applications must remain usable without MetaGen.

Initial decisions:

- Tool name: `NGIN.MetaGen`
- User command: `ngin metagen`
- First output: `NGIN.Reflection` metadata
- First input model: C++ attributes through NGIN macros
- First parser backend: Clang public tooling APIs, starting with `libclang`
- Clang dependency: optional for building the normal CLI
- Clang fork: no fork
- Binary distribution: provide prebuilt MetaGen-capable tooling later, do not
  vendor large Clang binaries into the source repo

## Goals

- Remove the need for handwritten `NginReflect(...)` declarations for opted-in
  project types.
- Make reflection authoring local to the C++ declarations that own the metadata.
- Keep generated output deterministic and reviewable.
- Keep the default NGIN build lightweight when Clang tooling is not installed.
- Leave a clear later path for DI/service generation without mixing it into the
  first reflection-focused slice.

## Non-Goals For The First Slice

- Do not generate DI factories or service registration metadata yet.
- Do not integrate MetaGen automatically into `ngin build` yet.
- Do not parse arbitrary unannotated types.
- Do not implement a custom C++ parser.
- Do not fork Clang or require a compiler plugin.
- Do not make `NGIN.Reflection` mandatory for plain native projects.

## User-Facing Model

Users opt in by annotating declarations with NGIN macros. The macros should live
in a small public header, for example:

```cpp
#include <NGIN/MetaGen/Annotations.hpp>

struct NGIN_REFLECT(name = "Game::Player")
Player {
  NGIN_FIELD(name = "display_name")
  std::string displayName;

  NGIN_IGNORE
  int transientDebugCounter{};

  NGIN_CTOR
  Player();
};
```

The macros should expand to Clang annotations when compiling with Clang tooling,
for example `[[clang::annotate("ngin.reflect:name=Game::Player")]]`, and degrade
to a compiler-safe no-op where the annotation form is not available.

The first attribute vocabulary should cover full reflection metadata:

- `NGIN_REFLECT(...)` for type opt-in and type-level options
- `NGIN_FIELD(...)`
- `NGIN_PROPERTY(...)`
- `NGIN_METHOD(...)`
- `NGIN_CTOR(...)`
- `NGIN_ENUM_VALUE(...)`
- `NGIN_BASE(...)`
- `NGIN_IGNORE`

Supported options should start with:

- `name`
- `category`
- `readonly`
- `required`

Unknown options should be rejected by MetaGen with a source location diagnostic.
The macro layer may accept token-like syntax, but the emitted annotation payload
should use one canonical string format so the parser is simple and stable.

## CLI Contract

Add:

```bash
ngin metagen --project <file.nginproj> --profile <name> --output <dir>
```

Behavior:

- `--project` and `--profile` resolve the same way as `validate`, `build`,
  and `graph`.
- `--output` defaults to `.ngin/metagen/<Project>/<Profile>/`.
- The command scans the selected project only in the first slice.
- The command emits `<Project>.reflection.generated.cpp`.
- The command prints generated file paths on success.
- If the CLI was built without Clang tooling support, the command exits with a
  clear diagnostic explaining how to enable or install MetaGen support.

The command should be available in help even when MetaGen support is unavailable.
That keeps scripts and documentation stable.

## Implementation Architecture

### CLI Integration

Keep the first implementation inside `Tools/NGIN.CLI`:

- Add a new `ngin_cli_metagen` library.
- Link the main `ngin_cli` target against it.
- Add `CmdMetaGen(...)` and dispatch it from `main.cpp`.
- Gate the real implementation behind CMake detection for Clang.
- Provide a stub implementation when Clang is unavailable.

This avoids introducing a second executable and lets future generated-build
integration call the same code path.

### Clang Integration

Use Clang's public tooling APIs against the project sources. The MVP starts
with the `libclang` C API because it exposes annotation cursors with a smaller
build and link surface than the full C++ LibTooling stack.

Preferred compile command source order:

1. Existing generated-mode CMake build directory for the selected project and
   configuration.
2. User-provided future override, if added later.
3. Clear failure asking the user to run a build/configure step first.

The first slice should not create or configure a generated CMake build only to
obtain compile commands. That would make `ngin metagen` too side-effectful and
harder to reason about.

### Source Selection

Use the selected project manifest:

- If `<Build><Sources>` is present, scan those files.
- Otherwise scan selected typed `<Sources>` entries, falling back to legacy
  `<SourceRoots>` when present.
- Do not scan dependency packages or referenced projects in the first slice.
- Do not scan generated output directories.

Header-only declarations are discoverable only when included by scanned
translation units. A later phase may add explicit header scanning.

### Metadata Model

Introduce internal model types such as:

- `MetaGenType`
- `MetaGenField`
- `MetaGenProperty`
- `MetaGenMethod`
- `MetaGenConstructor`
- `MetaGenEnumValue`
- `MetaGenBase`
- `MetaGenDiagnostic`

Each model item should carry:

- source location
- C++ qualified name
- generated reflection name
- options from annotations
- enough typed spelling to emit `TypeBuilder<T>` calls

Use this model as the boundary between Clang AST collection and code emission.

### Code Emission

Emit deterministic C++:

- Include `NGIN/Reflection/Reflection.hpp`.
- Include source/header files required to name the reflected types.
- Create one generated registration function for the project.
- Register all collected types through `NGIN::Reflection::ModuleRegistration`.
- Emit `TypeBuilder<T>` calls for generated fields, methods, constructors, enum
  values, bases, and attributes.

The generated file should not define handwritten `NginReflect(...)` overloads
unless that proves necessary. Prefer generated registration code that directly
uses `TypeBuilder<T>` so generated metadata remains isolated from user overload
sets.

Generated file naming:

```text
<output>/<Project>.reflection.generated.cpp
```

The generated C++ file is a build artifact. Do not require users to check it in.

## Build And Distribution Strategy

### Making It Easy To Build

The normal repo build should remain easy:

- `cmake --preset dev`
- `cmake --build build/dev --target ngin_cli`

If Clang tooling is found, `ngin_cli` includes MetaGen support automatically. If
not, the command exists but reports that MetaGen support is unavailable.

CMake option:

```cmake
option(NGIN_CLI_ENABLE_METAGEN "Enable MetaGen support in the native CLI." ON)
```

CMake behavior:

- `NGIN_CLI_ENABLE_METAGEN=OFF`: always build the stub.
- `NGIN_CLI_ENABLE_METAGEN=ON` and Clang found: build real MetaGen.
- `NGIN_CLI_ENABLE_METAGEN=ON` and Clang missing: build the stub and print a
  configure-time status message, not a fatal error.

This keeps contributors without LLVM installed unblocked.

### Clang Version Policy

Use upstream Clang/LLVM through CMake packages.

Initial supported policy:

- Prefer installed Clang 18 or newer.
- Accept newer Clang versions when CMake package discovery succeeds.
- Do not rely on unstable private Clang APIs unless no public API exists.
- Keep the Clang-facing implementation isolated in `ngin_cli_metagen`.

### Bundling Binaries

Do not vendor Clang/LLVM binaries into this repository.

Longer-term distribution should use release artifacts:

- Publish MetaGen-capable NGIN tool binaries for common platforms.
- Document package-manager based setup for source builds.
- Let source builds use system LLVM/Clang.

Rationale:

- Clang/LLVM binaries are large.
- Vendoring them would complicate repo size, updates, licensing notices, and
  platform coverage.
- Prebuilt NGIN tooling gives users the easy path without making every source
  checkout heavy.

### Forking Clang

Do not fork Clang.

MetaGen should use supported Clang APIs and Clang annotations. Forking
Clang would create long-term maintenance cost, version lag, and platform
packaging complexity. If NGIN eventually needs syntax beyond
`[[clang::annotate]]`, prefer macros, generated sidecar metadata, or an upstream
Clang extension path before considering any fork.

## Phased Roadmap

### Phase 1: Command And Stub

- Add `ngin metagen` command.
- Add CMake option and Clang detection.
- Add unavailable stub behavior.
- Add CLI help and tests for the unavailable path.

### Phase 2: Attribute Model And Parser

- Add annotation macro header.
- Implement annotation payload parser.
- Implement Clang AST traversal for annotated declarations.
- Emit an intermediate metadata model.
- Add tests for parsing and source diagnostics.

### Phase 3: Reflection Code Generation

- Emit `<Project>.reflection.generated.cpp`.
- Support types, fields, properties, methods, constructors, enums, and bases.
- Add fixture project tests that run MetaGen and compile generated output.

### Phase 4: Build Integration

- Add project manifest opt-in for generated metadata.
- Teach generated-mode CMake emission to run MetaGen before compiling project
  targets.
- Add generated files to target sources.
- Ensure incremental rebuilds happen when annotated source changes.

### Phase 5: DI And Services

- Add service and injection annotations.
- Emit typed DI construction traits/factories.
- Integrate with the DI extension points introduced after Phase 2 of the
  NGIN.Core DI plan.

## Open Design Notes For Later

- Decide exact project manifest schema for enabling MetaGen during `ngin build`.
- Decide whether generated registration should be auto-linked through static
  initializers, explicit app startup calls, or package bootstrap.
- Decide how to scan public headers that are not included by any compiled source.
- Decide whether generated metadata should include source locations for editor
  tooling.
- Decide how generated modules should be named across applications, libraries,
  packages, and plugins.

## Verification Expectations

For documentation-only changes to this plan, no build is required.

When implementing MetaGen:

- Build `ngin_cli`.
- Run `NGINCliTests`.
- Run `ngin metagen` against a small annotated fixture.
- Compile the generated reflection file in a test target when Clang support is
  enabled.
