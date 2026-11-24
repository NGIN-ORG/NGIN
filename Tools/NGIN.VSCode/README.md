# NGIN Tools for VS Code

NGIN Tools makes the normal NGIN C++ loop feel native in VS Code:

> Open a source file → edit → save → see diagnostics → press F5.

The extension owns editor interaction and applies reviewed workspace edits. The
native `ngin` CLI remains authoritative for manifests, effective Build Context,
file membership, composition, packages, lifecycle plans, and run intent.

## Creating and navigating projects

**Create Project** uses the same templates as `ngin new`. Choose an executable,
static library, shared library, interface library, or plugin, then choose a
split, colocated, public/private, or custom directory layout.

**Add Package** searches the workspace by package, export, and provided capability.
Choose a package for its default exports or select one typed export. Review the
proposed `<Uses>` edit before applying it. In a saved manifest, **Go to Definition**
and **Find All References** navigate between local package declarations, exports,
and project references.

**Check Setup** asks whether you intend to build or debug. It checks the selected
project and tools and offers actions for a missing CMake preset, pending configure,
or missing C/C++ debug adapter.

## Workspace view

The NGIN Activity Bar contains one project-scoped **Workspace** view. Authored
workspaces contain native Executable and Library projects plus explicitly
registered CMake project directories; loose `.nginproj` files appear under
**Standalone Products** when grouping is needed.

Expanding a product shows its physical folders and ordinary files directly.
Folders are enumerated lazily through the VS Code workspace filesystem, so a
root refresh never recursively walks every product. The product manifest is
opened from the product row rather than duplicated as a file child.

The active Build Context overlays semantics without hiding normal work:

- selected Build inputs show their kind;
- plausible C/C++ inputs not selected show a restrained **Not in build** state;
- `.env`, Markdown, JSON, scripts, licenses, and other support files remain
  ordinary files with no membership warning;
- Stage and Action inputs show their role;
- generated and external items are separated into read-only branches; and
- missing inputs and model failures appear under Issues and in Problems.

Nested products are boundaries rather than duplicated file trees. A broken
Composition Graph does not remove physical navigation or generic file creation.
Use **Locate Active File**, **Show Ignored Files**, and **Refresh Workspace** in
the view title as needed.

## Active Project and current-file ownership

The **Active Project** is explicitly selected per NGIN workspace and receives a
filled-check icon in the Workspace tree. It is the default for Build, tasks,
F5, and Ctrl+F5. NGIN supplies the shortcuts when there is no selected VS Code
debug configuration. Libraries may be selected; Run and Debug remain available only
when that project has Run intent.

File actions such as Analyze, Format, and Explain Build Membership use the
owner of the selected file. Opening a library file keeps Build, Run, and F5
pointed at the Active Project. Ambiguous ownership is chosen once and remembered. Running or
debugging another project from its row is a one-time target.

**Build Context** contains Configuration, Target, Toolchain, Profile, and
Option overrides. Run selection is launch state, not a compiler dimension. One
compact status item shows the effective project and Configuration; its tooltip
includes the complete context, selection source, and current operation.

CMake project rows show their selected configure preset and configuration,
physical files, File API targets/sources/artifacts, CTest tests, stale state,
and diagnostics. Select a preset explicitly, then use Configure, Build, Build
Target, or Test. CMake Run, Debug, Benchmark, Stage, and destructive Clean are
not offered without an explicit contract. Configure, Build, and Test require a
trusted VS Code workspace; untrusted workspaces retain safe physical
navigation and preset display metadata.

The CMake project and Tests rows expose Build Project through their context
menus. The Tests row can run the full CTest suite, and each built test row can
run only that test. A CTest `_NOT_BUILT` placeholder offers Build Project;
building refreshes the model and replaces placeholders with runnable tests.

Use **Copy CLI Command** in Project Actions to reproduce a Build, Run, or Test
selection in your shell (PowerShell on Windows). Choose **Explain Build
Membership** from a file’s NGIN menu to see matching rules and source locations.

## Files and authoring

**New File** and **New Folder** are physical operations. They never add a Build
or Stage rule, including for `.env`, Markdown, JSON, scripts, or unknown file
types.

Semantic commands include:

```text
NGIN: New C++ Source File
NGIN: New C++ Header File
NGIN: New C++ Class
NGIN: New C++ Module
NGIN: New C++ Item…
NGIN: Import C++ Items…
NGIN: Update Product Membership
```

Creation preserves a selected folder and supports co-located,
include/source-split, public/private, header-only, and custom layouts. New
projects offer these layouts explicitly. Interface Libraries never offer a
compiled Source item.

The CLI returns a versioned authoring plan. The extension previews multi-file
or manifest-changing work, rejects stale or dirty manifests, and applies files
and minimal text edits in one VS Code workspace edit. Existing globs are
recognized, so creation does not add redundant exact rules.

Product-file Rename, Move, Delete, and Duplicate use the same safety boundary;
ordinary-file duplication remains physical-only. Generated,
external, dependency-owned, cross-boundary, Stage-input, and Action-input cases
are rejected when the operation cannot be proven safe. Supported deletes can be
restored with VS Code Undo.

## Composition

The collapsed Composition branch shows packages, Actions, Runs, Tests, and
Benchmarks with provenance. Direct and transitive packages are distinct:
requirement changes and removal are available only for direct packages.
Restore and Lock remain native lifecycle actions. Direct requirement edits use
the same preconditioned, minimal-edit authoring plans as product files.

## Build, run, debug, test, and tooling

Build, Run, Debug, Test, Benchmark, and analysis configure automatically when
generated state is missing or stale. Lifecycle work uses cancellable progress,
Problems, NGIN Output, native tasks, native Testing, and the platform C++ debug
adapter. Opening a file never triggers Configure merely to populate the tree.

**Run Project** opens the program in the integrated terminal with standard
input. **Run with Arguments** accepts quoted arguments such as
`--name "two words"`. F5 uses the same CLI launch plan with the native debugger.
For a saved NGIN debug configuration, use `"type": "ngin", "request": "launch"`.

Expand a native project in Test Explorer to run or debug an individual named
registration. Its source link opens the Test declaration in the project manifest.
Running the project reports a result for each registration.

Compile-database-backed C++ configuration and structured analyzer diagnostics
remain active. Project analyzers and formatters require workspace trust and one
explicit enable decision; dependency locks are never bypassed.

For CMake projects, compiler identity, include paths, definitions, and command
fragments come from CMake File API compile groups. The extension does not
require or consume state from the CMake Tools extension.

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
development build and then `ngin` on `PATH`. Build, Run, tasks, and IntelliSense share the directory reported by
`ngin context`. Set `ngin.build.outputRoot` to place these build variants in another directory.

See the [editor protocol](../../docs/reference/editor-protocol.md),
[tooling guide](../../docs/guides/tooling.md), and
[CLI reference](../../docs/reference/cli.md).
