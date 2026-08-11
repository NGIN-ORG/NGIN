# VS Code developer-workflow overhaul

## Outcome

The extension is organized around the developer loop:

> Open a source file → edit → save → see diagnostics → press F5 to debug.

The CLI and resolved Composition Graph remain authoritative. VS Code infers
source context and presents results; it does not duplicate NGIN resolution.

## Implemented architecture

### Tooling protocol

- `ngin analyze --file <source>` narrows resolved Action inputs.
- Repeated `--file` values are supported.
- Headers select the closest Action translation unit.
- `ngin analyze --format json` emits only a stable
  `NGIN.ActionDiagnostics` envelope on standard output.
- Diagnostics contain file, start/end range, severity, Action identity, rule
  code, message, and an optional fix inventory.
- Multiple resolved analyzers merge into the same envelope without extension
  hard-coding.
- Configure and tool logs stay out of the JSON stream.

### Automatic source analysis

- Opening and saving C/C++ files schedules analysis.
- Save analysis is debounced and never runs on every keystroke.
- New work cancels obsolete work and waits for process termination before
  starting another analysis for the same project.
- Closing a document clears its analyzer diagnostics.
- Diagnostics survive failed/cancelled reanalysis and are replaced only by a
  successful result.
- Full-project analysis remains an explicit command.
- Manifest, compiler, and analyzer results have separate diagnostic owners.

### Trust and locks

- Workspace trust and Action policy remain mandatory.
- Project tooling requires one explicit enable decision.
- Approved tooling creates and reuses the selected output's dependency lock.
- Stale locks produce a refresh action rather than an implicit bypass.
- Explicit Enable Analyzers and Formatters and Lock Dependencies commands remain
  available.

### Project and file context

- One Projects view replaces duplicated Solution and Active Project views.
- Project tree expansion shows semantic product files, dependencies, launches,
  tooling, generated inputs, nested projects, and issues; it does not select a
  global context.
- The normal Explorer remains available for workspace-wide filesystem
  navigation.
- The default fallback project is independent from the project owning the
  active source file.
- File commands and F5 use source ownership first and the default project as a
  fallback.
- Equally specific ownership is chosen once and remembered.
- Configuration, target, toolchain, and options are remembered per project.

### Explorer and project UI

- Explorer decorations distinguish selected, unselected, generated, and owned
  project files.
- Explorer/editor menus expose include, exclude, analyze, format, and reveal
  owning project.
- Project rows expose only Build, Run, and Debug inline. Lifecycle, Test,
  Analyze, tooling, dependency, and advanced operations are project context
  actions rather than child rows.
- Project expansion exposes selected product files together with generated,
  missing, external, and nested-project composition state. The native Explorer
  remains the physical workspace browser.
- Graph internals are under collapsed Advanced composition and explicit
  inspect/explain commands.
- The status bar shows one effective project and Configuration, distinguishing
  active-file ownership from the default fallback.
- The accessible dashboard reports actionable readiness, selection, last
  analysis, launches, packages, and build inputs without replacing its DOM on
  every state change.
- Native welcome content and one walkthrough cover setup, project creation,
  build/debug, and trusted tooling.
- Analyzer and manifest diagnostics expose Quick Fix actions, and test-capable
  projects participate in VS Code's native Testing view.

### Configure, build, and debug

- Generated configure state is fingerprinted by BuildPlan and ActionPlan.
- Unchanged configure state and compile commands are reused.
- Build, stage, debug, and analysis configure as an implicit prerequisite.
- Explicit Configure forces regeneration for troubleshooting.
- F5 resolves the active file's project, builds/stages it, and delegates to the
  platform C++ debugger.
- Multiple Launch definitions use a remembered picker.

## Verification gates

- CLI parser and serializer unit coverage for structured Action diagnostics.
- VS Code unit coverage for the JSON envelope, compiler/manifest separation,
  ownership ordering, paths, graph parsing, and debug intent.
- Real `Hello.Analyzer` per-file execution proving clang-tidy JSON output.
- Repeated analysis proving configure-cache reuse.
- CLI focused test executable.
- VS Code typecheck, unit suite, extension-host integration suite, and VSIX
  packaging.

## Deliberately extensible surfaces

The envelope includes a fix inventory so fix-it code actions can be added
without replacing the protocol. Analyzer identity comes from each resolved
Action, allowing additional package analyzers to participate without a new VS
Code integration. Build adapters and package providers remain behind the
existing Composition Graph and derived-plan boundaries.
