---
name: test-cpp
description: Run the NGIN repository's canonical C++ test flows, including workspace ctest, workspace workflow, NGIN.Core tests, and smoke validation for App.NativeMinimal, App.HostedCore, and App.Basic. Use this when a task requires deterministic verification commands for this repo.
---

# Test C++

Use this skill when the task is to verify changes in this repository.

## Default Test Flows

- Workspace tests: `.ai/skills/test-cpp/scripts/test.sh workspace`
- Workspace workflow build: `.ai/skills/test-cpp/scripts/test.sh workflow`
- `NGIN.Core` tests: `.ai/skills/test-cpp/scripts/test.sh ngin-core`
- `App.NativeMinimal` smoke validation/build/run: `.ai/skills/test-cpp/scripts/test.sh app-native-minimal`
- `App.HostedCore` smoke validation/build/run: `.ai/skills/test-cpp/scripts/test.sh app-hosted-core`
- `App.Basic` smoke validation: `.ai/skills/test-cpp/scripts/test.sh app-basic`

## Workflow

1. Build the relevant target first.
2. Run the smallest verification scope that matches the change.
3. Prefer `Examples/App.NativeMinimal/` for plain CLI, generated build, and staging checks.
4. Prefer `Examples/App.HostedCore/` for hosted-runtime checks.
5. Use `Examples/App.Basic/` when you specifically need the compact older hosted/project-manifest runtime path.

## Notes

- Workspace tests use `ctest --test-dir build/dev --output-on-failure`.
- `NGIN.Core` tests use the `build/ngin-core-ci` tree and `Release` config.
- Manifest/schema/package dependency changes should validate at least `App.NativeMinimal` and `App.HostedCore`.
- Runtime-host changes should run `NGIN.Core` tests plus `App.HostedCore`.
- Generated CMake/build changes should run `App.NativeMinimal`; add `App.HostedCore` when package linking is affected.
- The `app-basic` flow validates and then builds the staged layout through generated project CMake from `App.Basic.nginproj`.
