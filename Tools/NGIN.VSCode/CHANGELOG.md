# Change Log

## Unreleased

- Invalid selections persisted by older extension builds now fall back to the
  current workspace choices instead of being passed to the CLI.
- Added direct project-row selection, Configure/Build/Run/Debug/Rebuild/Clean
  context actions, file context actions, generator inputs and outputs in
  Product files, and an actionable state for projects whose model cannot load.
- Fixed clean VSIX packaging by explicitly pinning VSCE's Secretlint preset.
- Simplified the Projects sidebar around the normal developer loop: compact
  Configuration and lifecycle state, one Product files group, versioned
  dependencies, Run selection only when needed, conditional issues, and direct
  access to the current output folder.
- Redesigned project navigation, default-project context, Explorer decorations,
  lifecycle feedback, onboarding, input flows, and the Project Overview.
- Added schema-derived manifest choices, project creation, manifest and analyzer
  Quick Fix providers, and native VS Code Testing Run/Debug profiles.
- Removed normal success notifications and unsolicited modal tooling prompts.
- Prevented failed Composition Graph requests from re-running during tree
  refreshes and added one-time guidance when the selected CLI is incompatible.
- Made project rows select on click, added Configure/Build/Run/Debug status-bar
  actions, replaced Product Files with role-based groups, and added direct C++
  source/header creation commands.
- Added a persistent Project/Files view toggle so non-product files and folders
  can be edited and managed without leaving the NGIN project pane.
- Hide and guard Run/Debug unless the selected project declares resolved Run
  intent, while keeping Configure and Build available for libraries.

- Added one compact Projects view with a project-scoped file browser, inline
  Build, Run, and Debug buttons, project context actions, and optional
  Composition Details.
- Added source ownership inference without mutating the default fallback
  project, plus a searchable project picker and remembered ambiguous ownership.
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
  Run selection, and graph-derived arguments, environment, and working
  directory.
- Added compile-database-backed Microsoft C/C++ configuration with graph
  fallback data.
- Simplified the status bar to the pinned target and Configuration, and
  simplified the dashboard to developer-facing readiness and tooling state.
- Added generated-metadata XML completion and hovers, CLI validation on save,
  comment-preserving formatting, and lossless authoring commands.
- Added focused unit tests and a real VS Code extension-host smoke test.
