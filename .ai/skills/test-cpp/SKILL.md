---
name: test-cpp
description: Run the NGIN repository's canonical C++ test flows, including focused CLI tests, workspace ctest, workspace workflow, NGIN.Core tests, and smoke validation for Hello.Native, Hello.Hosted, and Hello.Reflection.
---

# Test C++

Use this skill when the task is to verify changes in this repository.

## Default Test Flows

- Workspace tests: `.ai/skills/test-cpp/scripts/test.sh workspace`
- Focused CLI tests: `.ai/skills/test-cpp/scripts/test.sh cli`
- Workspace workflow build: `.ai/skills/test-cpp/scripts/test.sh workflow`
- `NGIN.Core` tests: `.ai/skills/test-cpp/scripts/test.sh ngin-core`
- `Hello.Native` smoke validation/build/run: `.ai/skills/test-cpp/scripts/test.sh hello-native`
- `Hello.Hosted` smoke validation/build/run: `.ai/skills/test-cpp/scripts/test.sh hello-hosted`
- `Hello.Reflection` smoke validation/build/run: `.ai/skills/test-cpp/scripts/test.sh hello-reflection`

## Workflow

1. Build the relevant target first.
2. Run the smallest verification scope that matches the change.
3. Prefer `Examples/Hello.Native/` for plain CLI, generated build, and staging checks.
4. Prefer `Examples/Hello.Hosted/` for hosted-runtime checks.
5. Use `Examples/Hello.Reflection/` for reflection code-generation checks.
6. Use `cli` for CLI authoring, resolution, package restore, graph/inspect,
   staging, launch, command, and test-suite changes.

## Notes

- Workspace tests use `ctest --test-dir build/dev --output-on-failure`.
- Focused CLI tests run `./build/dev/Tools/NGIN.CLI/tests/NGINCliTests`.
- `NGIN.Core` tests use the `build/ngin-core-ci` tree and `Release` config.
- Manifest/schema/package dependency changes should run focused CLI tests, then
  validate or build the relevant `Hello.*` example.
- Runtime-host changes should run `NGIN.Core` tests plus `Hello.Hosted`.
- Generated CMake/build changes should run `Hello.Native`; add `Hello.Hosted`
  when package linking is affected.
- Reflection/generator changes should run `Hello.Reflection`.
