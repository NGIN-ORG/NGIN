# NGIN Tools for VS Code

NGIN Tools is the graph-driven VS Code experience for NGIN workspaces,
projects, packages, and the native C++ development loop.

The extension does not implement a second NGIN project model. It owns editor
interaction and delegates meaning to two repository-owned contracts:

- generated `ManifestSpec` metadata drives XML completion and hover help; and
- the native `ngin` CLI supplies validation, the resolved Composition Graph,
  and every executable lifecycle operation.

This boundary allows the extension to provide a rich IDE without letting the
editor, command line, and CI disagree about the project.

## Experience

The NGIN activity-bar container contains:

- **Solution**, for discovered workspaces and projects, active Configuration,
  Target, Toolchain, Presets, and common lifecycle actions;
- **Active Project**, for graph-derived product, selection, files, packages,
  Exports, Options, Capabilities, Actions, Plugins, staging, Launch, Testing,
  Publish, and graph-edge state; and
- **Project Dashboard**, a resolved project overview with direct XML,
  validation, inspection, configure, build, run, and debug actions.

The status bar keeps the active project and complete selection visible. A
graph-resolution failure is displayed as an error state rather than leaving
apparently valid stale information on screen.

## Start

Build the native CLI and extension from the repository:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
cd Tools/NGIN.VSCode
npm install
npm run build
```

Open the repository in VS Code and run the extension development host, or
install the VSIX produced by:

```bash
npm run package
```

When `ngin.executable` is empty, the extension first looks for the repository's
`build/dev/Tools/NGIN.CLI/ngin` development build and then uses `ngin` from
`PATH`. Set `ngin.executable` for another build.

## Selection and output

Workspace discovery reads `<Projects>` rules from `.ngin`; it does not treat
every nearby project as a workspace member. A folder without a workspace still
supports standalone `.nginproj` files.

One active context supplies every view, command, task, debug request, and C/C++
configuration:

- Workspace and Project
- Configuration
- Target
- Toolchain
- explicit Option overrides

Presets are command-specific in the CLI, so **NGIN: Run Preset** executes the
Preset's declared command as a one-shot action. It does not silently turn a
build Preset into persistent selection for validate, run, or publish.

Extension-driven output defaults to:

```text
<workspace>/build/ngin/<project>/<Configuration>.<Target>.<Toolchain>
```

Change the root with `ngin.build.outputRoot`.

## Build, run, test, and publish

Commands use argument arrays through the native CLI and share the exact active
context:

```text
NGIN: Restore Packages
NGIN: Lock Dependencies
NGIN: Configure
NGIN: Build
NGIN: Rebuild
NGIN: Clean Output
NGIN: Stage
NGIN: Run
NGIN: Debug
NGIN: Test
NGIN: Publish
NGIN: Analyze
NGIN: Run Format Actions
```

Configure, Build, Run, and Debug are also available directly on each project
row in Solution. Analyzer and formatter Actions use the generated
`<output>/ngin.lock`; when it is missing, the extension asks before creating it
from the active Composition.

Running a process requires a trusted workspace. Output is streamed to the NGIN
output channel, operations are cancellable, and manifest diagnostics are
attached to exact files, lines, columns, and NGIN diagnostic codes.

The `ngin` task provider supplies configure, build, stage, test, and restore
tasks. These use the same context and output directory as the views and command
palette.

## Debugging

The contributed `ngin` debug type is a configuration resolver rather than a
debug adapter. It:

1. resolves the current Composition Graph;
2. builds and stages the product when `preStage` is enabled;
3. selects the default graph Launch;
4. applies its working directory, arguments, environment, and staged library
   path; and
5. hands off to `cppvsdbg` on Windows or `cppdbg` with GDB/LLDB elsewhere.

The current graph is sufficient for Product launches. Debugging a package Tool
launch remains unavailable until the CLI exposes the resolved LaunchPlan,
because the extension will not guess a provider-owned Tool artifact path.

## C/C++ IntelliSense

NGIN configure/build asks CMake to generate:

```text
<output>/cmake/compile_commands.json
```

When Microsoft's C/C++ extension is installed and `ngin.cpp.enabled` is true,
NGIN registers a custom configuration provider. It maps each translation
unit's compiler, arguments, include directories, defines, forced includes, and
language standard. Headers use the closest translation unit. Graph build facts
provide a useful fallback before configure has produced the compile database.

The extension intentionally does not generate `c_cpp_properties.json`; the
active graph and compile database remain the only configuration sources.

## XML authoring

All `.ngin`, `.nginproj`, and `.nginpkg` files receive:

- XML syntax highlighting and snippets;
- context-aware element and attribute completion;
- element and attribute hover information;
- CLI semantic validation on demand and on save; and
- comment-preserving CLI formatting.

Generated schemas and editor metadata live under `schemas/`. Regenerate them
with `ngin_manifest_schema_generator` after changing `ManifestSpec`.

The Project Files tree combines physical files with graph membership and shows
selected, unselected, generated, external, missing, authored-manifest, and
nested-project-boundary states. File commands include create, rename,
duplicate, trash-delete, reveal, include, and exclude.

Membership actions add exact typed `<Source>`, `<Header>`, `<CxxModule>`, or
`<Resource>` rules through a VS Code `WorkspaceEdit`. They preserve comments,
globs, and extension elements and leave the manifest dirty for review and
undo. Saving invokes the CLI validator. The XML utility only finds edit ranges;
it never evaluates Refinements, dependencies, capabilities, or backend rules.

## Inspection and authoring commands

```text
NGIN: Show Composition Graph
NGIN: Inspect Active Project
NGIN: Explain Selection
NGIN: Diff Composition Against Project
NGIN: Open Project Dashboard
NGIN: Edit Product
NGIN: Add Package
NGIN: Add Project Reference
NGIN: Add Action
NGIN: Format Manifest
```

Dependency, project-reference, and Action authoring delegates to the CLI's
canonical authoring commands.

## Develop and verify

```bash
npm run typecheck
npm run test:unit
npm run test:integration
npm run build
npm run package
```

The unit suite covers graph envelopes, diagnostics, workspace vocabulary,
compile commands, paths, and lossless membership edits. The extension-host
suite activates the packaged entry point against this repository, discovers
`Hello.Native`, opens its real Composition Graph, checks tasks, and validates
through the native CLI.

See the [extension implementation plan](../../docs/plans/vscode-extension-reimagining-plan.md),
[CLI reference](../../docs/reference/cli.md), and
[Composition Graph reference](../../docs/reference/composition-graph.md).
