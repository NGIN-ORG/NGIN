---
name: build-cpp
description: Configure and build the NGIN C++ workspace, CLI, CLI tests, NGIN.Core package, or canonical Hello.Native/Hello.Hosted/Hello.Reflection examples using the repo's canonical CMake and ngin commands.
---

# Build C++

Use this skill when the task is to configure or build C++ code in this repository.

## Default Targets

- Workspace configure: `.ai/skills/build-cpp/scripts/configure.sh workspace`
- Workspace CLI build: `.ai/skills/build-cpp/scripts/build.sh cli`
- CLI test executable build: `.ai/skills/build-cpp/scripts/build.sh cli-tests`
- Workspace workflow build: `.ai/skills/build-cpp/scripts/build.sh workflow`
- `NGIN.Core` configure: `.ai/skills/build-cpp/scripts/configure.sh ngin-core`
- `NGIN.Core` test build: `.ai/skills/build-cpp/scripts/build.sh ngin-core-tests`
- Plain native example build: `.ai/skills/build-cpp/scripts/build.sh hello-native`
- Hosted Core example build: `.ai/skills/build-cpp/scripts/build.sh hello-hosted`
- Reflection example build: `.ai/skills/build-cpp/scripts/build.sh hello-reflection`

## Workflow

1. Start with `AGENTS.md` at the repo root.
2. If the task touches CLI or manifests, configure the workspace and build `ngin_cli`.
3. If the task touches workspace flow, build `ngin.workflow`.
4. If the task touches `Packages/NGIN.Core/`, configure and build `NGIN.Core` tests.
5. If the task touches generated project builds, staging, or the tooling/runtime boundary, build `Hello.Native`.
6. If the task touches `NGIN.Core` hosted runtime behavior or package linking, build `Hello.Hosted`.
7. If the task touches reflection code generation, build `Hello.Reflection`.
8. If the task touches CLI authoring, resolution, restore, graph, inspect,
   command behavior, or tests, build `NGINCliTests`.

## Notes

- The workspace preset is `dev`.
- Generated output lives under `build/`.
- The active manifest model is product-first V4; do not add legacy V1/V2/V3
  compatibility build paths.
- Use `Examples/Hello.Native/` for plain NGIN tooling and generated-build validation.
- Use `Examples/Hello.Hosted/` for hosted `NGIN.Core` validation.
- Use `Examples/Hello.Reflection/` for reflection code-generation validation.
