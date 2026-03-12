---
name: test-cpp
description: Run the NGIN repository's canonical C++ test flows, including workspace ctest, the workspace workflow, NGIN.Core tests, and App.Basic smoke validation. Use this when a task requires deterministic verification commands for this repo.
---

# Test C++

Use this skill when the task is to verify changes in this repository.

## Default Test Flows

- Workspace tests: `.ai/skills/test-cpp/scripts/test.sh workspace`
- Workspace workflow build: `.ai/skills/test-cpp/scripts/test.sh workflow`
- `NGIN.Core` tests: `.ai/skills/test-cpp/scripts/test.sh ngin-core`
- `App.Basic` smoke validation: `.ai/skills/test-cpp/scripts/test.sh app-basic`

## Workflow

1. Build the relevant target first.
2. Run the smallest verification scope that matches the change.
3. Prefer `Examples/App.Basic/` for CLI and manifest smoke checks.

## Notes

- Workspace tests use `ctest --test-dir build/dev --output-on-failure`.
- `NGIN.Core` tests use the `build/ngin-core-ci` tree and `Release` config.
- The `app-basic` flow validates and then builds the staged layout.
