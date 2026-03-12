# NGIN AI Contribution Guide

This file is the root instruction surface for AI-assisted work in this repository.

## Architecture Overview

NGIN is a modular C++ application platform built around authored workspace, project, and package manifests that resolve into a staged runtime layout. The repository combines a native CLI, package wrappers, a locally owned runtime package, and source-backed dependency trees that are composed through the workspace model.

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

Treat these as generated outputs unless the user explicitly asks otherwise:

- `build/`
- staged output under `.ngin/build/` or `build/**/stage/`
- `*.ngintarget`

Do not edit generated files to implement behavior changes.

## Where Changes Usually Belong

- CLI command behavior: `Tools/NGIN.CLI/src/main.cpp`
- Workspace automation and tests: root `CMakeLists.txt` and `cmake/`
- Canonical example changes: `Examples/App.Basic/`
- Host/runtime behavior: `Packages/NGIN.Core/`
- Package exposure or source binding changes: `Packages/*.nginpkg`
- First-party dependency implementation changes: `Dependencies/NGIN/*`

Before changing manifests or CLI semantics, check the relevant spec in `docs/specs/`.

## Dependency Editing Policy

- `Dependencies/NGIN/*` may be modified when the required implementation genuinely belongs to that source tree.
- If a dependency subtree contains its own `AGENTS.md`, follow that file for any edits under that subtree.
- Prefer changing package wrappers in `Packages/` when the task is about exposure, binding, packaging, or workspace composition rather than dependency internals.
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
./build/dev/Tools/NGIN.CLI/ngin project validate --project Examples/App.Basic/App.Basic.nginproj --variant Runtime
./build/dev/Tools/NGIN.CLI/ngin project build --project Examples/App.Basic/App.Basic.nginproj --variant Runtime --output build/manual/App.Basic
```

## Working Rules

- Keep diffs narrow and task-focused.
- Prefer updating docs when behavior or contracts change.
- Preserve the distinction between package wrappers in `Packages/` and source trees in `Dependencies/`.
- Use `Examples/App.Basic/` for CLI and manifest smoke checks unless a different example is required.
- When changing dependency subtrees, follow any local `AGENTS.md` in that subtree.

## Typical Change Workflow

1. Read the relevant contract in `docs/specs/` and the nearest applicable README or `AGENTS.md`.
2. Identify the ownership boundary for the change before editing code or manifests.
3. Modify the implementation in the correct repo area with a narrow diff.
4. Build the smallest relevant target for the change.
5. Run the smallest relevant verification flow and report anything not verified.

## Verification Expectations

Pick the smallest relevant verification set:

- CLI changes: build `ngin_cli`, run workspace `ctest`, and smoke-test `App.Basic`
- Workspace/build flow changes: run `ngin.workflow`
- `NGIN.Core` changes: build and run `NGINCoreTests`
- Manifest/schema changes: validate and graph `Examples/App.Basic/App.Basic.nginproj`

If you cannot run a verification step, state that explicitly.
