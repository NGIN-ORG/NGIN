# Change Log

## Unreleased

- Added one compact Projects view with a project-scoped file browser, inline
  Build, Run, and Debug buttons, project context actions, and optional
  Composition Details.
- Added source ownership inference without mutating the pinned Build target,
  plus a searchable Build-target switcher and remembered ambiguous ownership.
- Added automatic open/save analysis through the CLI's structured Action
  diagnostics protocol, with cancellation, debouncing, per-file narrowing,
  header translation-unit selection, and full-project analysis.
- Added independent manifest, compiler, and analyzer Problems collections with
  exact locations, analyzer identities, and rule codes.
- Added one-time verified tooling consent, automatic dependency-lock reuse,
  explicit enable/lock commands, and one-click stale-lock refresh without
  weakening workspace trust policy.
- Added NGIN membership decorations and include, exclude, analyze, format, and
  owning-project context commands to the native Explorer while retaining the
  project-scoped file browser.
- Added incremental configure-state reuse. Build, stage, debug, and analysis
  configure implicitly when needed; explicit Configure remains available.
- Added F5 source-project resolution, staged native debugging, remembered
  Launch selection, and graph-derived arguments, environment, and working
  directory.
- Added compile-database-backed Microsoft C/C++ configuration with graph
  fallback data.
- Simplified the status bar to the pinned target and Configuration, and
  simplified the dashboard to developer-facing readiness and tooling state.
- Added generated-metadata XML completion and hovers, CLI validation on save,
  comment-preserving formatting, and lossless authoring commands.
- Added focused unit tests and a real VS Code extension-host smoke test.
