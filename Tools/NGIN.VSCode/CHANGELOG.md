# Change Log

## Unreleased

- Reimagined the extension around one persisted Workspace, Project,
  Configuration, Target, Toolchain, Option, and output context.
- Added native Solution and Active Project views backed by workspace discovery
  and the canonical Composition Graph.
- Added graph-derived packages, Exports, Options, Capabilities, Actions,
  Plugins, staging, Launch, Testing, Publish, provenance, and edge views.
- Added status-bar selection and lifecycle controls plus a resolved Project
  Dashboard.
- Added validate, restore, configure, build, clean, rebuild, stage, run, debug,
  test, publish, analyze, graph, inspect, explain, and cancellation workflows.
- Exposed Configure and Debug beside Build and Run on Solution projects, and
  added an explicit dependency-lock workflow for trusted tooling Actions.
- Added an `ngin` task provider using the same active context.
- Added native C++ debug configuration resolution from staged Product and
  Launch facts.
- Added optional Microsoft C/C++ custom configuration support from the
  generated compile database with graph-based fallback data.
- Added physical Project Files browsing with selected, unselected, generated,
  external, missing, authored, and nested-project-boundary states.
- Added guarded create, rename, duplicate, trash-delete, reveal, include, and
  exclude project-file operations using lossless VS Code XML edits.
- Added CLI-backed package, project-reference, and Action authoring.
- Added lossless Product Name, Type, Version, and Linkage editing plus
  command-line Option overrides.
- Added generated-metadata completion and hover support, CLI diagnostics on
  save, and comment-preserving formatting.
- Restored focused unit tests and a real VS Code extension-host smoke test.
