# NGIN Copilot Instructions

Use `AGENTS.md` at the repository root as the primary instruction file.

Key rules:

- Check the matching `docs/reference/` page, current code, focused tests, and
  canonical examples before changing CLI semantics, manifest structure, or
  package/runtime behavior.
- Prefer `Examples/Hello.Native/` for plain project validation and smoke tests;
  use `Hello.Hosted` or `Hello.Reflection` for runtime or generator work.
- Treat `build/`, staged output, and `*.nginlaunch` as generated artifacts.
- `Packages/` mostly contains package wrappers; `Packages/NGIN.Core/` is the main locally owned runtime package.
- `Dependencies/NGIN/*` may contain their own `AGENTS.md`; follow subtree instructions when present.

Canonical commands:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
cmake --build build/dev --target ngin.workflow
ctest --test-dir build/dev --output-on-failure
```
