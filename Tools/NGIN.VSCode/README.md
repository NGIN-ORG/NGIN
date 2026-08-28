# NGIN Tools for VS Code

NGIN Tools makes the normal NGIN C++ loop feel native in VS Code:

> Open a source file -> edit -> save -> see diagnostics -> press F5.

The extension owns editor interaction while the native `ngin` CLI remains the
authority for manifests, composition, packages, builds, Actions, staging, and
run intent.

## Projects and effective context

The NGIN Activity Bar contains one compact **Projects** view. Each project row
shows its Configuration and current state when known, with Build, Run, and
Debug kept inline.
Expansion is limited to Product files, dependencies, multiple Run
configurations when selection matters, and issues when something needs
attention. The native Explorer remains the physical workspace browser.

A project row reports one of two context states:

- **active file** means the project owns the file in the active editor and is
  the effective project for normal commands;
- **selected** means the project is the fallback when the active file has no
  owner.

Selecting or expanding a tree row never changes the default project. Use
**Select Project** from the project row's context menu; it sets the fallback
used when the active file has no owner. Ambiguous file ownership is chosen once
and remembered.

Project rows expose only valid Build, Run, and Debug actions. Their context
menu starts with **Select Project** and directly provides Configure, Build,
Run, Debug, Rebuild, and Clean. It can also open the manifest or current output
folder. Libraries and other non-runable products do not show Run or Debug.
Advanced operations remain in Project Actions and the Command Palette.

## One project-context status item

NGIN contributes one status item:

```text
$(project) Hello.Native · Debug
```

It identifies the effective project and Configuration. Its tooltip explains
whether that project comes from active-file ownership or the default fallback,
and includes Target, Toolchain, analysis state, and the last operation.

During work it becomes a spinner for the active operation; model or analyzer
problems use a warning icon and accessible text. Click the item to open
**NGIN: Project Actions**.

## Project Actions

The searchable action hub groups commands by purpose:

- **Lifecycle:** Build, Run, Debug, Test, and Benchmark;
- **Build context:** Configuration, Target, Toolchain, and Run;
- **Project:** manifest, default selection, packages, and semantic C++ file creation;
- **Diagnostics:** Problems, NGIN Output, and Check Setup;
- **Advanced:** Configure, Stage, restore, lock, inspect, explain, diff,
  publish, analyze, and format.

Impossible actions are omitted. Configuration and Run choices are remembered
per project build context. All individual commands remain searchable through
the Command Palette for expert and keyboard workflows.

## Files and authoring

Use Explorer for physical navigation, rename, delete, and other filesystem
operations. NGIN adds semantic Explorer/editor actions for analysis, formatting,
product membership, and revealing the owning project.

**Update Product Membership** inspects the resolved graph and performs the one
valid Include or Exclude edit. **New C++ Source File** and **New C++ Header
File** show the target and manifest change before creating anything, then add
the new file to the product without rewriting unrelated XML.

`.ngin`, `.nginproj`, and `.nginpkg` files receive syntax highlighting,
metadata-driven completion and hovers, CLI validation on save, Quick Fixes, and
comment-preserving formatting.

## Build, run, debug, and test

Build, Run, Debug, Test, Benchmark, and analysis configure automatically when generated
state is missing or stale. **Configure Project** forces regeneration for
troubleshooting.

F5 debugs the project owning the active source file and falls back to the
default project only when the active file has no owner. Ctrl+F5 uses the same
ownership rule. When several Run
definitions are valid, NGIN asks once and remembers the choice for that
project, Configuration, Target, and Toolchain. Run and Debug share the same
selection.

Lifecycle progress appears on the project and the single status item. Routine
success stays quiet. Failures populate Problems and offer **Open Problems** and
**Show Output** without forcing Output to take focus. Detailed process logs are
retained at `<output>/diagnostics/<command>.log`. Set
`ngin.revealOutputOnRun` to `true` if lifecycle Output should open on start.

`ngin.output.verbosity` controls what the NGIN Output channel retains:

- `compact` (default) keeps lifecycle summaries, user-requested output, and
  failures while hiding successful background commands and JSON payloads;
- `commands` also records background CLI command lines;
- `trace` records raw stdout/stderr and protocol payloads for troubleshooting.

Test-capable products participate in VS Code's native Testing view. Debugging
uses `cppvsdbg` on Windows and `cppdbg` with GDB or LLDB elsewhere. The Microsoft
C/C++ extension supplies the native adapters and receives compile-database-backed
IntelliSense configuration from NGIN. Opening a file never starts Configure:
NGIN responds immediately with graph-derived fallback settings, then uses an
existing compile database after Build or Configure has generated one. Exact
compile entries also associate package and dependency sources with the active
application, so navigating from a compiler diagnostic retains the same include
paths, definitions, compiler, and language mode used by that build.

## Analysis and trusted tooling

When a resolved Composition contains an Analyze Action, opening or saving a
C/C++ source file schedules debounced analysis. Obsolete work is cancelled and
only one analysis per project runs at a time. Results appear as squiggles,
hover text, Problems entries, and supported Quick Fixes.

Project analyzers and formatters require workspace trust and one explicit
enable decision. Approval creates and reuses the selected output's dependency
lock. A stale lock offers **Refresh Tooling Lock**; policy is never bypassed.
Analysis waits and retries quietly while another NGIN lifecycle operation owns
the CLI.

## Getting started and setup

The Projects welcome state offers one primary **Create NGIN Project** action,
plus **Check Setup** and **Open Getting Started**. The four-step walkthrough uses
theme-aware illustrations and completes from verified CLI, discovered project,
successful lifecycle, and tooling-decision state.

**NGIN: Check Setup** reports:

- resolved CLI path and version;
- workspace trust;
- project discovery;
- Composition Graph readiness;
- direct Settings, Refresh, and Output actions.

## Important commands

```text
NGIN: Open Project Actions
NGIN: Set Default Project
NGIN: Select Configuration
NGIN: Select Platform Target
NGIN: Select Toolchain
NGIN: Select Run
NGIN: Select Profile
NGIN: Build
NGIN: Run
NGIN: Run with Arguments
NGIN: Debug
NGIN: Test
NGIN: Benchmark
NGIN: Analyze File
NGIN: Analyze Project
NGIN: Format File
NGIN: Format Manifest
NGIN: Check Setup
NGIN: Open Resolved Project JSON
NGIN: Inspect Project
NGIN: Create Project
NGIN: New C++ Source File
NGIN: New C++ Header File
```

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
<workspace>/.ngin/build/<project>/<Configuration>.<Target>.<Toolchain>
```

Change the root with `ngin.build.outputRoot`.

See the [UI/UX refresh plan](../../docs/plans/vscode-extension-ui-ux-refresh-plan.md),
[tooling guide](../../docs/guides/tooling.md), and
[CLI reference](../../docs/reference/cli.md).
