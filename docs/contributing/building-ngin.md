# Build NGIN

NGIN uses CMake presets for repository development.

## Requirements

- Git and initialized submodules
- CMake 3.20 or newer
- Ninja
- a C++23-capable compiler

Reflection metadata generation has no additional compiler-library dependency;
it preprocesses annotated headers with the target's selected C++ compiler.

## Submodules

To check out the dependency commits recorded by this repository:

```bash
git submodule update --init --recursive
```

This normally leaves submodules with a detached HEAD, which is expected for
pinned dependency checkouts. To develop on the first-party libraries or update
them to the latest `main`, start with clean submodule working trees and run:

```bash
git submodule foreach 'git switch main'
git submodule update --remote --merge
```

The first-party submodules explicitly select `main` in `.gitmodules`.
`--remote` fetches that branch instead of using the recorded dependency commit;
`--merge` keeps the current branch attached. Review and test dependency updates,
then commit the changed submodule pointers in this repository. Commit any library
source changes inside the corresponding submodule first.

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
