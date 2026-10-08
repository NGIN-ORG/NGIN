# NGIN Agent Guide

Repository-wide instructions for AI contributors. A nearer `AGENTS.md` overrides
these within its subtree (currently `Dependencies/NGIN/NGIN.Base/` and
`Dependencies/NGIN/NGIN.Reflection/`).

## Project Model

NGIN is a modular C++ project system and application toolkit. The `ngin` CLI
resolves authored project, package, and workspace manifests into a Composition
Graph, which drives build, generation, staging, runtime, testing, publishing, and
editor tooling.

Keep these rules intact unless the task explicitly changes them:

- One `.nginproj` represents one physical product, rooted in `<Executable>` or
  `<Library Kind="Static|Shared|Interface|Plugin">`.
- Product behavior belongs in semantic sections such as `<Build>`, `<Stage>`,
  `<Run>`, `<Test>`, and `<Benchmark>`.
- The resolved Composition Graph is the semantic source of truth.
- CMake is a generated build backend, not the application authoring model.
- Do not reintroduce Project wrappers, Module products, root-level compatibility
  grammar, or older manifest models unless a migration is explicitly requested.
- Applications may use `ngin` tooling without linking any NGIN runtime library;
  `NGIN.Core`, `NGIN.Reflection`, `NGIN.ECS`, `NGIN.UI`, etc. are optional.

## Where Things Live

| Path | Owns |
| --- | --- |
| `Tools/NGIN.CLI/` | `ngin` CLI: authoring model, resolution, graph, staging, launch |
| `Tools/NGIN.CLI/tests/` | Focused CLI tests (`NGINCliTests`) and fixtures |
| `Tools/NGIN.VSCode/` | VS Code extension |
| `Packages/` | Package wrappers (`*.nginpkg`), provider integration, locally owned packages |
| `Packages/NGIN.Core/` | Application host/runtime package |
| `Dependencies/NGIN/` | First-party libraries — **git submodules**, see below |
| `Dependencies/ThirdParty/` | Vendored third-party source; do not modify unless the task requires it |
| `Examples/` | Canonical examples and smoke-test projects |
| `docs/` | `guides/`, `reference/` (contracts), `architecture/decisions/`, `contributing/` |
| `build/`, `.ngin/build/` | Generated output |

Put changes in their natural ownership layer. Reusable low-level abstractions
belong in the appropriate `Dependencies/NGIN/` library, not duplicated higher up.
Use `Packages/` wrappers for exposure, binding, providers, and workspace
composition rather than editing dependency internals.

**Submodules:** `Dependencies/NGIN/{NGIN.Base,NGIN.Log,NGIN.Reflection,NGIN.ECS}`
are separate repositories. Changes there must be committed in the submodule and
then the submodule pointer updated here. Mention this when reporting such changes.

## Source of Truth

Before editing, read the narrowest relevant sources: nearby code and tests,
canonical examples, `docs/reference/` for contracts, and
`docs/architecture/decisions/` for durable decisions. Prefer current
implementation, schemas, and tests over Git history. If docs and implementation
disagree, call out the conflict instead of silently picking one.

Never implement behavior by editing generated output (`build/`, `.ngin/build/`,
staged runtime layouts, generated backend files, `*.nginlaunch`); change the
authored source or the generator.

## C++ Conventions

Follow the nearest `.clang-format` where one exists; otherwise match the
surrounding file (indent width differs between projects). Across first-party code:

- Types and functions `PascalCase`; locals and aggregate fields `camelCase`;
  private class members `m_name`, statics `s_name`.
- Braces on their own line for namespaces, classes, and functions.
- C++23. Match local idioms (e.g. trailing return types in the CLI).

## Change Rules

- Make the smallest coherent change; avoid drive-by refactors and compatibility shims.
- Do not weaken validation or error handling to make tests pass.
- New third-party dependencies, schema concepts, compatibility layers, and broad
  ownership restructuring require explicit user direction.
- Update docs under `docs/` and canonical examples when public behavior,
  manifests, CLI contracts, or APIs change.
- Add or update focused, behavior-level tests for new behavior and regressions.

## Build and Verify

Canonical scripts live in `.ai/skills/build-cpp/` and `.ai/skills/test-cpp/`
(exposed as the `build-cpp` / `test-cpp` skills). Full details:
`docs/contributing/building-ngin.md` and `docs/contributing/testing.md`.

Reuse the existing `build/dev` tree; reconfigure only if it is missing or
configure-time inputs changed. Don't build during exploration — batch edits,
then verify once.

```bash
cmake --preset dev                                      # configure (if needed)
cmake --build build/dev --target ngin_cli               # CLI
cmake --build build/dev --target NGINCliTests && ./build/dev/Tools/NGIN.CLI/tests/NGINCliTests
cmake --build build/dev --target ngin.workflow          # workspace composition
ctest --test-dir build/dev --output-on-failure          # workspace tests
```

Pick verification by affected surface:

| Change | Verify with |
| --- | --- |
| CLI, manifests, resolution, graph, restore, staging, launch | `NGINCliTests`, then validate the relevant `Hello.*` example |
| Generated build / staging | `Examples/Hello.Native/` |
| `NGIN.Core` / hosted runtime | `NGIN.Core` tests (`test-cpp ngin-core`) + `Examples/Hello.Hosted/` |
| Reflection / MetaGen | Reflection tests + `Examples/Hello.Reflection/` |
| Workspace composition | `ngin.workflow` target |
| Docs only | No build; check links, commands, and terminology |

Escalate to broader tests only when the narrow check exposes wider risk or the
user asks. Report exactly what ran and what remains unverified.

## Git

Do not commit, push, branch, or rewrite history unless asked. Leave unrelated
local changes (including in submodules) untouched.
