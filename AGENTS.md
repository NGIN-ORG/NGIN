# NGIN AI Contribution Guide

This file is the root instruction surface for AI-assisted work in this repository.

## Architecture Overview

NGIN is a modular C++ application platform built around authored workspace, project, and package manifests that resolve into a staged runtime layout. The repository combines a native CLI, package wrappers, a locally owned runtime package, and source-backed dependency trees that are composed through the workspace model.
In the active model, normal application projects build from `.nginproj` metadata through generated CMake backend input. Handwritten `CMakeLists.txt` files are mainly for workspace automation, package wrappers, dependency-owned source trees, or explicit manual compatibility paths.

## Start Here

Read these first when the task touches behavior or contracts:

1. `README.md`
2. `Tools/README.md`
3. `docs/specs/006-cli-contract.md` for CLI behavior
4. `docs/specs/010-workspace-and-project-model.md` for workspace and project structure
5. `docs/specs/003-package-manifest-and-runtime-contributions.md` for package/runtime composition

`docs/specs/` are the authoritative contracts for repository behavior. Do not modify spec files unless the task explicitly requires a contract, schema, or behavior definition change.

If a subtree contains its own `AGENTS.md`, that file overrides this one for files in that subtree.
Current known subtree instructions exist in:

- `Dependencies/NGIN/NGIN.Base/AGENTS.md`
- `Dependencies/NGIN/NGIN.Reflection/AGENTS.md`

## Repo Map

- `Tools/NGIN.CLI/`: native `ngin` CLI implementation
- `Examples/App.Basic/`: canonical smoke-test application and manifest example
- `Packages/NGIN.Core/`: locally owned host/runtime package
- `Packages/*`: workspace package wrappers and package metadata
- `Dependencies/NGIN/*`: externally developed first-party source trees
- `Dependencies/ThirdParty/*`: third-party source trees
- `docs/specs/*`: authoritative contract and design docs
- `build/`: generated build output

## Authored Vs Generated

Treat these as authored inputs:

- `*.ngin`
- `*.nginproj`
- `*.nginpkg`
- `CMakeLists.txt`
- files under `docs/`, `Tools/`, `Packages/`, `Examples/`, and `Dependencies/`

Project-level handwritten `CMakeLists.txt` files are no longer the normal app authoring path. Prefer `.nginproj` root `<Build>` metadata for new project build behavior.

Treat these as generated outputs unless the user explicitly asks otherwise:

- `build/`
- staged output under `.ngin/build/` or `build/**/stage/`
- `*.nginlaunch`

Do not edit generated files to implement behavior changes.

## Where Changes Usually Belong

- CLI command behavior: `Tools/NGIN.CLI/src/main.cpp`
- Workspace automation and tests: root `CMakeLists.txt` and `cmake/`
- Canonical example changes: `Examples/App.Basic/`
- Host/runtime behavior: `Packages/NGIN.Core/`
- Package exposure, provider wiring, and backend integration changes: `Packages/*.nginpkg`
- First-party dependency implementation changes: `Dependencies/NGIN/*`

Before changing manifests or CLI semantics, check the relevant spec in `docs/specs/`.

## Dependency Editing Policy

- `Dependencies/NGIN/*` may be modified when the required implementation genuinely belongs to that source tree.
- If a dependency subtree contains its own `AGENTS.md`, follow that file for any edits under that subtree.
- Prefer changing package wrappers in `Packages/` when the task is about exposure, binding, packaging, generated build integration, or workspace composition rather than dependency internals.
- Avoid editing `Dependencies/ThirdParty/*` unless the task explicitly requires vendored third-party source changes.
- Do not make opportunistic dependency edits just because the code is reachable from the workspace; keep changes in the ownership boundary where they belong.

## Canonical Commands

Configure the workspace:

```bash
cmake --preset dev
```

Build the CLI:

```bash
cmake --build build/dev --target ngin_cli
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

Smoke-test the example project:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate --project Examples/App.Basic/App.Basic.nginproj --profile Runtime
./build/dev/Tools/NGIN.CLI/ngin build --project Examples/App.Basic/App.Basic.nginproj --profile Runtime --output build/manual/App.Basic
```

## Working Rules

- Keep diffs narrow and task-focused.
- Prefer updating docs when behavior or contracts change.
- Preserve the distinction between package wrappers in `Packages/` and source trees in `Dependencies/`.
- Use `Examples/App.Basic/` for CLI and manifest smoke checks unless a different example is required.
- Prefer project `<Build>` metadata over adding new handwritten project `CMakeLists.txt` files.
- Use workspace `PackageProviders` together with package `<Build Mode="...">` metadata to integrate external or dependency-owned CMake projects.
- When changing dependency subtrees, follow any local `AGENTS.md` in that subtree.

## Build Timing Policy

- The commands in this file are reference commands, not an automatic checklist to run after every edit.
- Do not build during initial exploration or after each individual edit.
- Batch related edits first, then run one targeted verification pass near the end of the task.
- Reuse existing configured build trees when possible. Do not rerun `cmake --preset dev` unless the build tree is missing, broken, or the change affects configure-time inputs.
- Prefer the cheapest command that can validate the specific change.
- Escalate to broader verification only when the narrower check fails, the change affects build or workspace composition, or the user explicitly asks for broader validation.
- For docs, comments, prompt files, and other non-behavioral instruction changes, do not build unless explicitly requested.

## Typical Change Workflow

1. Read the relevant contract in `docs/specs/` and the nearest applicable README or `AGENTS.md`.
2. Identify the ownership boundary for the change before editing code or manifests.
3. Modify the implementation in the correct repo area with a narrow diff, batching related edits before verification.
4. Run one final targeted build or validation step for the change rather than building after each edit.
5. Escalate to broader verification only when warranted by change scope, failure, or explicit user request, and report anything not verified.

## Verification Expectations

Pick one final verification path that matches the change. Do not run all of these by default.

- CLI changes:
  - First choice: build `ngin_cli`
  - Add workspace `ctest` only when the change affects shared CLI behavior, test-covered flows, or regression risk is broad
  - Add `App.Basic` smoke checks only when the change affects project, build, staging, or runtime behavior
- Workspace/build flow changes: run `ngin.workflow`
- `NGIN.Core` changes: build and run `NGINCoreTests`
- Manifest/schema changes:
  - First choice: validate `Examples/App.Basic/App.Basic.nginproj`
  - Build or graph the example project only when generation, staging, or runtime layout behavior changed
- Docs or AI-instruction changes: no build required

If you cannot run a verification step, state that explicitly.
