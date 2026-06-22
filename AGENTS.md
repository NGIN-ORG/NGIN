# NGIN AI Contribution Guide

This file is the root instruction surface for AI-assisted work in this repository.

## Architecture Overview

NGIN is a modular C++ application platform built around authored workspace,
project, and package manifests that resolve into a staged runtime layout. The
repository combines a native CLI, package wrappers, a locally owned runtime
package, and source-backed dependency trees that are composed through the
workspace model.

The active project model is the fully breaking, product-first V4 direction:
one `.nginproj` describes one primary product identity, such as an
`Application`, `Library`, `Tool`, `Test`, `Benchmark`, `Plugin`, `Module`, or
`External`. Project behavior belongs inside product sections such as
`<Application><Build>...</Build></Application>`, not legacy root-level
normalized sections. The resolved Composition Graph is the source of truth for
build, packages, generation, staging, runtime, tests, launch, publish, editor
tooling, diff, and explanation.

Handwritten `CMakeLists.txt` files are mainly for workspace automation, package
wrappers, dependency-owned source trees, or explicit manual compatibility
paths.

## Start Here

Read these first when the task touches behavior or contracts:

1. `README.md`
2. `Tools/README.md`
3. `docs/plans/NGIN-V4-North-Star-And-Subsystem-Plans.md` for the active V4
   product-first contract
4. `docs/plans/NGIN-V4-Implementation-Workstreams.md` for implementation
   sequencing
5. `docs/plans/NGIN-V4-Implementation-Progress.md` for what has already
   changed in the repo
6. `docs/specs/006-cli-contract.md` for CLI behavior that has not yet been
   superseded by the V4 plans
7. `docs/specs/010-workspace-and-project-model.md` and
   `docs/specs/003-package-manifest-and-runtime-contributions.md` only as
   legacy/background context when the V4 plans do not cover a detail

The V4 plan/progress files are currently authoritative for active manifest,
workspace, package, graph, and package-restore behavior. Older `docs/specs/`
files may still describe V3-era shapes. Do not preserve or reintroduce V1/V2/V3
compatibility paths, including fallback code paths that silently keep legacy
manifest behavior alive, unless the user explicitly asks for a legacy migration
tool.

The canonical instruction filename is `AGENTS.md`. Before editing files in any
subtree, search upward and within the target subtree for the nearest
`AGENTS.md`; the nearest file overrides this one for files under that subtree.
Current known subtree instructions include:

- `Dependencies/NGIN/NGIN.Base/AGENTS.md`
- `Dependencies/NGIN/NGIN.Reflection/AGENTS.md`

## Repo Map

- `Tools/NGIN.CLI/`: native `ngin` CLI implementation
- `Tools/NGIN.CLI/tests/`: focused CLI test files by area; do not recreate a
  monolithic `AllTests.cpp`
- `Examples/Hello.Native/`: plain product/build/stage smoke-test application
- `Examples/Hello.Hosted/`: hosted `NGIN.Core` runtime smoke-test application
- `Examples/Hello.Reflection/`: reflection generator/package-tool smoke-test
  application
- `Packages/NGIN.Core/`: locally owned host/runtime package
- `Packages/*`: workspace package wrappers and package metadata
- `Dependencies/NGIN/*`: first-party source trees under full repo control
- `Dependencies/ThirdParty/*`: third-party source trees
- `docs/specs/*`: older and background contract docs; verify active behavior
  against the V4 plan/progress files before following V3-era shapes
- `build/`: generated CMake, test, manual, and staged build output

## Authored Vs Generated

Treat these as authored inputs:

- `*.ngin`
- `*.nginproj`
- `*.nginpkg`
- `CMakeLists.txt`
- files under `docs/`, `Tools/`, `Packages/`, `Examples/`, and `Dependencies/`

Project-level handwritten `CMakeLists.txt` files are no longer the normal app
authoring path. Prefer product-first `.nginproj` sections for new project build
behavior, for example `<Application><Build>...</Build></Application>` or
`<Library><Build>...</Build></Library>`.

Treat these as generated outputs unless the user explicitly asks otherwise:

- `build/` and all contents under it, including root CMake output, test output,
  manual build output, and staged runtime layouts
- staged output under `.ngin/build/` or `build/**/stage/`
- `*.nginlaunch`

Do not edit generated files to implement behavior changes.

## Where Changes Usually Belong

- CLI command behavior: `Tools/NGIN.CLI/src/main.cpp`
- CLI model, authoring, resolution, restore, graph, and command behavior:
  `Tools/NGIN.CLI/src/`
- CLI tests: add coverage to the focused file under `Tools/NGIN.CLI/tests/`
  (`AuthoringTests.cpp`, `WorkspaceTests.cpp`, `PackageTests.cpp`,
  `GraphInspectTests.cpp`, etc.)
- Workspace automation and tests: root `CMakeLists.txt` and `cmake/`
- Canonical plain example changes: `Examples/Hello.Native/`
- Canonical hosted runtime example changes: `Examples/Hello.Hosted/`
- Canonical reflection/generator example changes: `Examples/Hello.Reflection/`
- Host/runtime behavior: `Packages/NGIN.Core/`
- Package exposure, provider wiring, and backend integration changes: `Packages/*.nginpkg`
- First-party dependency implementation changes: `Dependencies/NGIN/*`

Before changing manifests or CLI semantics, check the active V4 plan/progress
files first. Use `docs/specs/` only for still-applicable background contracts.

## Dependency Editing Policy

- `Dependencies/NGIN/*` may be modified when the required implementation
  genuinely belongs to that source tree.
- `Dependencies/NGIN/*` is under full project control. It is appropriate to add
  general foundation abstractions there when the need belongs there, such as
  allocators, containers, reflection primitives, or other reusable platform
  facilities.
- `Dependencies/NGIN/NGIN.Base` is the core foundation library used across the
  platform; prefer it for broadly useful low-level abstractions instead of
  duplicating local utilities in higher layers.
- If a dependency subtree contains its own `AGENTS.md`, follow that file for any
  edits under that subtree.
- Prefer changing package wrappers in `Packages/` when the task is about
  exposure, binding, packaging, generated build integration, or workspace
  composition rather than dependency internals.
- Avoid editing `Dependencies/ThirdParty/*` unless the task explicitly requires
  vendored third-party source changes.
- Do not make opportunistic dependency edits just because the code is reachable
  from the workspace; keep changes in the ownership boundary where they belong.

## When Unsure

- Prefer the active V4 plan/progress files and current code over older
  `docs/specs/` material.
- Treat `docs/specs/` as background unless the active V4 docs do not cover the
  detail.
- If docs conflict with code, report the conflict and the files involved before
  relying on a guess.
- If the product direction is ambiguous, keep the change narrow and ask for
  confirmation rather than inventing compatibility behavior.

## Dangerous Changes

Get explicit user confirmation before:

- adding migrations or legacy compatibility layers
- broad restructuring across ownership boundaries
- schema redesigns or manifest shape changes beyond the requested behavior
- deleting legacy paths, compatibility code, or authored examples
- introducing new third-party or platform dependencies

When a new dependency is genuinely needed, justify why the existing standard
library, repo utilities, or first-party `Dependencies/NGIN/*` libraries are not
enough.

## Canonical Commands

Configure the workspace:

```bash
cmake --preset dev
```

Build the CLI:

```bash
cmake --build build/dev --target ngin_cli
```

Build and run the focused CLI test executable:

```bash
cmake --build build/dev --target NGINCliTests
./build/dev/Tools/NGIN.CLI/tests/NGINCliTests
```

Run the standard workspace flow:

```bash
cmake --build build/dev --target ngin.workflow
```

Run workspace tests:

```bash
ctest --test-dir build/dev --output-on-failure
```

Build and test `NGIN.Core`:

```bash
cmake -S Packages/NGIN.Core -B build/ngin-core-ci -DNGIN_CORE_BUILD_TESTS=ON -DNGIN_CORE_BUILD_EXAMPLES=OFF
cmake --build build/ngin-core-ci --config Release --target NGINCoreTests
ctest --test-dir build/ngin-core-ci --output-on-failure -C Release
```

Smoke-test the plain native example:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate --project Examples/Hello.Native/Hello.Native.nginproj --profile Debug
./build/dev/Tools/NGIN.CLI/ngin build --project Examples/Hello.Native/Hello.Native.nginproj --profile Debug --output build/manual/Hello.Native
```

Smoke-test the hosted runtime example:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate --project Examples/Hello.Hosted/Hello.Hosted.nginproj --profile Debug
./build/dev/Tools/NGIN.CLI/ngin build --project Examples/Hello.Hosted/Hello.Hosted.nginproj --profile Debug --output build/manual/Hello.Hosted
```

## Working Rules

- Keep diffs narrow and task-focused.
- Prefer updating docs when behavior or contracts change.
- Preserve the distinction between package wrappers in `Packages/` and source
  trees in `Dependencies/`.
- Do not introduce new dependencies without explicit justification.
- Inspect existing tests before creating new test files, and prefer adding to
  focused test files that already cover the area.
- Prefer narrow, behavior-focused tests over snapshot-heavy tests or broad
  integration tests.
- When public CLI behavior changes, update docs and tests in the same change.
- When manifest behavior changes, update the canonical examples that demonstrate
  that behavior.
- Use `Examples/Hello.Native/` for plain CLI/build/stage smoke checks.
- Use `Examples/Hello.Hosted/` when hosted runtime, `NGIN.Core`, package
  linking, runtime modules, or runtime staging are involved.
- Use `Examples/Hello.Reflection/` when generator, host-tool package, or
  reflection code-generation behavior is involved.
- Prefer product-first `.nginproj` metadata over adding new handwritten project
  `CMakeLists.txt` files.
- Use workspace `PackageProviders` together with package `<Build Mode="...">`
  metadata to integrate external or dependency-owned CMake projects.
- Before editing any subtree, find and follow the nearest `AGENTS.md`.
- Avoid preserving V3 compatibility accidentally through permissive parsing,
  implicit fallbacks, or root-level manifest behavior.

## Do Not

- Do not edit generated files such as `build/`, `.ngin/build/`, staged layouts,
  or `*.nginlaunch` to implement behavior.
- Do not add new root-level manifest sections when product-first V4 sections
  are the intended contract.
- Do not create monolithic test files when a focused test file exists.
- Do not modify `Dependencies/ThirdParty/*` opportunistically.
- Do not run every canonical command unless the user explicitly asks for broad
  verification.

## Build Timing Policy

- The commands in this file are reference commands, not an automatic checklist
  to run after every edit.
- Do not build during initial exploration or after each individual edit.
- Batch related edits first, then run one targeted verification pass near the end of the task.
- Reuse existing configured build trees when possible. Do not rerun
  `cmake --preset dev` unless the build tree is missing, broken, or the change
  affects configure-time inputs.
- Prefer the cheapest command that can validate the specific change.
- Escalate to broader verification only when the narrower check fails, the
  change affects build or workspace composition, or the user explicitly asks for
  broader validation.
- For docs, comments, prompt files, and other non-behavioral instruction
  changes, do not build unless explicitly requested.

## Typical Change Workflow

1. Find the nearest applicable `AGENTS.md` and read the active V4 plan/progress
   files before relying on `docs/specs/`.
2. Identify the ownership boundary for the change before editing code or manifests.
3. Inspect existing tests in the affected area before adding or creating tests.
4. Modify the implementation in the correct repo area with a narrow diff,
   batching related edits before verification.
5. Run one final targeted build or validation step for the change rather than
   building after each edit.
6. Escalate to broader verification only when warranted by change scope, failure,
   or explicit user request, and report anything not verified.

## Verification Expectations

Pick one final verification path that matches the change. Do not run all of these by default.

- CLI changes:
  - First choice: build `ngin_cli`
  - Run `NGINCliTests` when the change affects CLI authoring, resolution,
    package restore, graph/inspect, staging, launch, or command behavior
  - Add workspace `ctest` only when the change affects shared CLI behavior,
    test-covered flows, or regression risk is broad
  - Add `Hello.Native` smoke checks for plain build/stage behavior
  - Add `Hello.Hosted` smoke checks when runtime/package linking is involved
  - Add `Hello.Reflection` smoke checks when generator/tool packages are
    involved
- Workspace/build flow changes: run `ngin.workflow`
- `NGIN.Core` changes: build and run `NGINCoreTests`
- Manifest/schema changes:
  - First choice: validate `Examples/Hello.Native/Hello.Native.nginproj`
  - Also validate `Hello.Hosted` or `Hello.Reflection` when the touched surface
    includes runtime or generation
  - Build or graph the example project only when generation, staging, or
    runtime layout behavior changed
- Docs or AI-instruction changes: no build required

If you cannot run a verification step, state that explicitly. If verification
fails, report the exact command and the relevant failure summary.
