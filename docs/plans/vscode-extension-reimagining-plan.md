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
- Explicit Enable Project Tooling and Lock Dependencies commands remain
  available.

### Project and file context

- One Projects view replaces duplicated Solution and Active Project views.
- The normal Explorer remains the primary filesystem interface.
- Project tree expansion is navigation/inspection; it does not select a global
  context.
- The pinned Build/Run target is independent from the project owning the
  active source file.
- File commands and F5 use source ownership first and the pinned target as a
  fallback.
- Equally specific ownership is chosen once and remembered.
- Configuration, target, toolchain, and options are remembered per project.

### Explorer and project UI

- Explorer decorations distinguish selected, unselected, generated, and owned
  project files.
- Explorer/editor menus expose include, exclude, analyze, format, and reveal
  owning project.
- Project rows expose only Build, Run, and Debug inline; Test and Analyze are
  visible when applicable, while advanced commands are under More.
- Graph internals are under collapsed Composition Details and explicit
  inspect/explain commands.
- The status bar shows only the pinned project and Configuration.
- The dashboard reports readiness, last analysis, launches, packages, and
  build inputs rather than duplicating the graph tree.

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
