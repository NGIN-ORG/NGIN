# NGIN Tools for VS Code

NGIN Tools makes the normal NGIN C++ loop feel native in VS Code:

> Open a source file → edit → save → see diagnostics → press F5 to debug.

The extension owns editor interaction while the native `ngin` CLI remains the
semantic authority for manifests, composition, packages, builds, Actions,
staging, and launch intent.

## Developer experience

The NGIN activity bar contains one compact **Projects** view. Click a project
row to select it; use the disclosure arrow to expand it. The selected project
drives normal Configure, Build, Run, and Debug commands. Expanded projects show
the manifest directly, followed by clear Sources, Headers, Resources,
dependencies, launch configurations, tooling, generated files, external
inputs, nested projects, and issues.

The view-title toggle switches between **Project** and **Files** modes. Project
mode shows the build-oriented structure above. Files mode shows the complete
physical project directory, including `.env`, notes, scripts, and files not
included in the product. From Files mode you can create files and folders or
rename, duplicate, and delete project-directory items. Generated output and
repository metadata directories remain hidden to keep the tree usable. The
chosen mode is remembered for the workspace.

Use **New C++ Source File** or **New C++ Header File** from a project's context
menu. The commands start in `src/` and `include/`, create missing directories,
and add the new file to the project manifest. The Sources and Headers groups
also expose the matching new-file button and show physical files that are not
yet included in the build.

The normal VS Code Explorer remains available as the workspace-wide browser.
NGIN decorates exceptional files there, such as generated inputs,
and contributes context menu commands to include, exclude, analyze, format,
and reveal the owning project. Set `ngin.decorations.mode` to `detailed` to show
ordinary included, excluded, and project-owned state.

The status bar shows the selected project and configuration, followed by
dedicated **Configure**, **Build**, **Run**, and **Debug** actions. Clicking the
project name opens **NGIN: Select Project**; while an operation is running,
clicking it cancels that operation.

File-specific operations and F5 instead use the project that owns the active
file. When equally specific projects own a file, the extension asks once and
remembers the answer. Project navigation, source ownership, and the selected
fallback project are separate concepts.

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
tooling is available, a non-modal prompt asks whether to enable it. On
approval it creates and reuses `<output>/ngin.lock`, and passes that lock to
analyzers and formatters automatically. If dependencies change, a one-click
**Refresh Tooling Lock** action repairs the stale lock.

Use **NGIN: Enable Analyzers and Formatters** to enable tooling after previously
declining, or **NGIN: Lock Dependencies** for explicit lock maintenance.

## Build, run, and debug

Build, Debug, analysis, and other consuming operations configure automatically
when their generated state is missing or stale. Unchanged configurations are
reused. **NGIN: Configure** remains available as an explicit regeneration and
troubleshooting command.

F5 debugs the project owning the active source file and falls back to the
selected project. Debugging builds and stages the product, resolves Launch
arguments, environment, working directory, and staged library paths, then
hands off to `cppvsdbg` on Windows or `cppdbg` with GDB/LLDB elsewhere. Multiple
Launch definitions get a remembered picker. Ctrl+F5 uses the same resolved
configuration without stopping in the debugger.

**NGIN: Run** starts immediately with the selected Launch definition. Use
**NGIN: Run with Arguments** to append one-off arguments. Run and Debug are
shown only for projects with resolved Launch intent; libraries and other
non-launchable products retain Configure and Build without offering actions
that the CLI cannot execute.

Lifecycle output uses compact `BUILD`, `RUN`, and `TEST` sections. Successful
backend steps are reduced to target, duration, and warning counts; compiler
warnings and errors remain available in the Problems view. Runtime records use
short timestamps and omit repetitive host and source metadata at Info level.
The unabridged process stream is retained under the selected output directory
at `diagnostics/<command>.log`.

The Microsoft C/C++ extension is required for native debugging. When installed,
NGIN also supplies compile-database-backed IntelliSense. Headers use the
closest translation unit and graph facts provide a fallback before the first
configuration.

## Project overview and composition details

The optional Project Overview is an accessible, keyboard-navigable action hub
for readiness, configuration, target, toolchain, analysis, recent operations,
launches, packages, and build inputs. It updates without resetting focus or
scroll position. Raw composition internals remain available through the
collapsed **Advanced composition** node, resolved-project JSON, Inspect,
Explain, and Diff commands.

## Onboarding, quick fixes, and tests

The Projects view provides native welcome content for empty workspaces, and the
single **Get started with NGIN** walkthrough covers CLI discovery, project
creation, build/debug, and verified tooling. **NGIN: Create Project** creates a
direct product-first manifest from CLI-generated editor metadata.

Manifest diagnostics offer Quick Fix actions for invalid product and linkage
values, package restore, and required product fields. Analyzer diagnostics can
apply structured fix inventories, including safe Fix All edits when an Action
provides them.

Test-capable NGIN projects appear in VS Code's Testing view with native Run and
Debug profiles. NGIN remains responsible for deriving the TestPlan, staging the
product, and executing it.

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
NGIN: Select Project
NGIN: Build
NGIN: Run
NGIN: Run with Arguments
NGIN: Debug
NGIN: Test
NGIN: Analyze File
NGIN: Analyze Project
NGIN: Format File
NGIN: Enable Analyzers and Formatters
NGIN: Open Resolved Project JSON
NGIN: Open Project Overview
NGIN: Create Project
NGIN: Show Files View
NGIN: Show Project View
NGIN: New C++ Source File
NGIN: New C++ Header File
```

See the [extension implementation plan](../../docs/plans/vscode-extension-reimagining-plan.md),
[tooling guide](../../docs/guides/tooling.md), and
[CLI reference](../../docs/reference/cli.md).
