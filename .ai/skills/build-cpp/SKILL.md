---
name: build-cpp
description: Configure and build the NGIN C++ workspace or the NGIN.Core package using the repo's canonical CMake commands. Use this when a task requires building the CLI, workspace workflow, or NGIN.Core tests without guessing the command sequence.
---

# Build C++

Use this skill when the task is to configure or build C++ code in this repository.

## Default Targets

- Workspace configure: `.ai/skills/build-cpp/scripts/configure.sh workspace`
- Workspace CLI build: `.ai/skills/build-cpp/scripts/build.sh cli`
- Workspace workflow build: `.ai/skills/build-cpp/scripts/build.sh workflow`
- `NGIN.Core` configure: `.ai/skills/build-cpp/scripts/configure.sh ngin-core`
- `NGIN.Core` test build: `.ai/skills/build-cpp/scripts/build.sh ngin-core-tests`

## Workflow

1. Start with `AGENTS.md` at the repo root.
2. If the task touches CLI or manifests, configure the workspace and build `ngin_cli`.
3. If the task touches workspace flow, build `ngin.workflow`.
4. If the task touches `Packages/NGIN.Core/`, configure and build `NGIN.Core` tests.

## Notes

- The workspace preset is `dev`.
- Generated output lives under `build/`.
- Use `Examples/App.Basic/` for smoke validation after a successful build. It exercises the generated project-CMake path from `.nginproj`.
