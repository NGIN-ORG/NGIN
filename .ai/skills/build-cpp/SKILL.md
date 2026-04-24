---
name: build-cpp
description: Configure and build the NGIN C++ workspace, NGIN.Core package, or canonical example applications using the repo's canonical CMake and ngin commands. Use this when a task requires building the CLI, workspace workflow, NGIN.Core tests, App.NativeMinimal, App.HostedCore, or App.Basic without guessing the command sequence.
---

# Build C++

Use this skill when the task is to configure or build C++ code in this repository.

## Default Targets

- Workspace configure: `.ai/skills/build-cpp/scripts/configure.sh workspace`
- Workspace CLI build: `.ai/skills/build-cpp/scripts/build.sh cli`
- Workspace workflow build: `.ai/skills/build-cpp/scripts/build.sh workflow`
- `NGIN.Core` configure: `.ai/skills/build-cpp/scripts/configure.sh ngin-core`
- `NGIN.Core` test build: `.ai/skills/build-cpp/scripts/build.sh ngin-core-tests`
- Plain native example build: `.ai/skills/build-cpp/scripts/build.sh app-native-minimal`
- Hosted Core example build: `.ai/skills/build-cpp/scripts/build.sh app-hosted-core`

## Workflow

1. Start with `AGENTS.md` at the repo root.
2. If the task touches CLI or manifests, configure the workspace and build `ngin_cli`.
3. If the task touches workspace flow, build `ngin.workflow`.
4. If the task touches `Packages/NGIN.Core/`, configure and build `NGIN.Core` tests.
5. If the task touches generated project builds, staging, or the tooling/runtime boundary, build `App.NativeMinimal`.
6. If the task touches `NGIN.Core` hosted runtime behavior or package linking, build `App.HostedCore`.

## Notes

- The workspace preset is `dev`.
- Generated output lives under `build/`.
- Use `Examples/App.NativeMinimal/` for plain NGIN tooling and generated-build validation.
- Use `Examples/App.HostedCore/` for hosted `NGIN.Core` validation.
- Use `Examples/App.Basic/` when you specifically need the compact older hosted/project-manifest runtime path.
