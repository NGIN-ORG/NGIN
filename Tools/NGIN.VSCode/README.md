# NGIN Tools for VS Code

NGIN Tools makes the normal NGIN C++ loop feel native in VS Code:

> Open a source file → edit → save → see diagnostics → press F5 to debug.

The extension owns editor interaction while the native `ngin` CLI remains the
semantic authority for manifests, composition, packages, builds, Actions,
staging, and launch intent.

## Developer experience

The NGIN activity bar contains one compact **Projects** view. It lists every
discovered project with Build, Run, Debug, optional Test and Analyze commands.
Advanced operations and graph details are collapsed under each project.

The normal VS Code Explorer remains the file browser. NGIN decorates files as
included, excluded, generated, or project-owned and contributes context menu
commands to include, exclude, analyze, format, and reveal the owning project.

The status bar shows only the pinned fallback Build/Run target and its
Configuration. Clicking it opens the searchable **NGIN: Switch Build Target**
picker. Opening a source file does not silently change that pinned target.

File-specific operations and F5 instead use the project that owns the active
file. When equally specific projects own a file, the extension asks once and
remembers the answer. Project selection for navigation, source ownership, and
the pinned execution target are separate concepts.

## Automatic source analysis

When a resolved Composition contains an Analyze Action, opening or saving a
C/C++ source file analyzes the relevant translation unit. Analysis is
debounced, obsolete work is cancelled, and only one analysis per project runs
at a time. Headers use the closest Action translation unit.

Analyzer results use the CLI's structured `NGIN.ActionDiagnostics` JSON
protocol. They appear as editor squiggles, hover text, and Problems entries
with the analyzer identity and rule code. Manifest, compiler, and analyzer
diagnostics use independent collections, so one operation cannot erase another
kind of problem.

Analysis is not run on every keystroke. Use `ngin.analysis.mode` to disable the
default open-and-save behavior, and `ngin.analysis.debounceMs` to change the
save delay. **NGIN: Analyze Project** remains available for full-project and CI
style checks.

## Trusted project tooling

The extension never weakens workspace Action policy. The first time automatic
tooling is needed, it asks whether to enable verified project tooling. On
approval it creates and reuses `<output>/ngin.lock`, and passes that lock to
analyzers and formatters automatically. If dependencies change, a one-click
**Refresh Tooling Lock** action repairs the stale lock.

Use **NGIN: Enable Project Tooling** to enable tooling after previously
declining, or **NGIN: Lock Dependencies** for explicit lock maintenance.

## Build, run, and debug

Build, Debug, analysis, and other consuming operations configure automatically
when their generated state is missing or stale. Unchanged configurations are
reused. **NGIN: Configure** remains available as an explicit regeneration and
troubleshooting command.

F5 debugs the project owning the active source file and falls back to the
pinned Build target. Debugging builds and stages the product, resolves Launch
arguments, environment, working directory, and staged library paths, then
hands off to `cppvsdbg` on Windows or `cppdbg` with GDB/LLDB elsewhere. Multiple
Launch definitions get a remembered picker. Ctrl+F5 uses the same resolved
configuration without stopping in the debugger.

The Microsoft C/C++ extension is required for native debugging. When installed,
NGIN also supplies compile-database-backed IntelliSense. Headers use the
closest translation unit and graph facts provide a fallback before the first
configuration.

## Project overview and composition details

The optional Project Overview shows only developer-facing state: product,
Configuration, readiness, last analyzer result, launches, packages, and build
inputs. Build, Run, Debug, and Test are primary actions. Composition graph
internals remain available through the collapsed **Composition Details** node,
graph JSON, Inspect, Explain, and Diff commands.

## XML authoring

`.ngin`, `.nginproj`, and `.nginpkg` files receive syntax highlighting,
metadata-driven completion and hovers, CLI validation on save, and
comment-preserving formatting. Package, project-reference, Action, Product,
Option, and exact file-membership edits preserve the authored XML rather than
creating a second editor-side semantic model.

## Build and install

```powershell
cmake --build build/dev --target ngin_cli
Set-Location Tools/NGIN.VSCode
npm install
npm test
npm run package
code --install-extension .\ngin-tools.vsix --force
```

When `ngin.executable` is empty, the extension first uses the repository
development build and then `ngin` on `PATH`. Extension output defaults to:

```text
<workspace>/build/ngin/<project>/<Configuration>.<Target>.<Toolchain>
```

Change the root with `ngin.build.outputRoot`.

## Important commands

```text
NGIN: Switch Build Target
NGIN: Build
NGIN: Run
NGIN: Debug
NGIN: Test
NGIN: Analyze File
NGIN: Analyze Project
NGIN: Format File
NGIN: Enable Project Tooling
NGIN: Show Composition Graph
NGIN: Open Project Dashboard
```

See the [extension implementation plan](../../docs/plans/vscode-extension-reimagining-plan.md),
[tooling guide](../../docs/guides/tooling.md), and
[CLI reference](../../docs/reference/cli.md).
