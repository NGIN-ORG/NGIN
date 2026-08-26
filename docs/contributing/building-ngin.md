# Build NGIN

NGIN uses CMake presets for repository development.

## Requirements

- Git and initialized submodules
- CMake 3.20 or newer
- Ninja
- a C++23-capable compiler

Reflection metadata generation has no additional compiler-library dependency;
it preprocesses annotated headers with the target's selected C++ compiler.

## Configure and build

```bash
cmake --preset dev
cmake --build build/dev
```

Build only the CLI while working on project-system code:

```bash
cmake --build build/dev --target ngin_cli
```

Do not edit anything under `build/`; it is generated output.

## Repository boundaries

- CLI and manifest behavior: `Tools/NGIN.CLI/`
- Package exposure and build integration: `Packages/*.nginpkg`
- Optional hosted runtime: `Packages/NGIN.Core/`
- First-party libraries: `Dependencies/NGIN/`
- Runnable behavior examples: `Examples/`

Read the nearest `AGENTS.md` before changing a subtree. The root
[`AGENTS.md`](../../AGENTS.md) contains the contribution and verification rules
for AI-assisted work.
