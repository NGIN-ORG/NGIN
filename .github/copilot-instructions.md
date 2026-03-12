# NGIN Copilot Instructions

Use `AGENTS.md` at the repository root as the primary instruction file.

Key rules:

- Check `docs/specs/` before changing CLI semantics, manifest structure, or package/runtime behavior.
- Prefer `Examples/App.Basic/` for validation and smoke testing.
- Treat `build/`, staged output, and `*.ngintarget` as generated artifacts.
- `Packages/` mostly contains package wrappers; `Packages/NGIN.Core/` is the main locally owned runtime package.
- `Dependencies/NGIN/*` may contain their own `AGENTS.md`; follow subtree instructions when present.

Canonical commands:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
cmake --build build/dev --target ngin.workflow
ctest --test-dir build/dev --output-on-failure
```
