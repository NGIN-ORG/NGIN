# NGIN Tools for VS Code

NGIN Tools makes the normal NGIN C++ loop feel native in VS Code:

> Open a source file → edit → save → see diagnostics → press F5.

The extension owns editor interaction and applies reviewed workspace edits. The
native `ngin` CLI remains authoritative for manifests, effective Build Context,
file membership, composition, packages, lifecycle plans, and run intent.

## Workspace view

The NGIN Activity Bar contains one product-scoped **Workspace** view. Authored
workspaces contain their Executable and Library products; loose `.nginproj`
files appear under **Standalone Products** when grouping is needed.

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
F5, and Ctrl+F5. Libraries may be selected; Run and Debug remain available only
when that project has Run intent.

When the current editor file belongs to another product, file-scoped Build,
Analyze, and Format actions use that owner without silently changing the Active
Project. Ambiguous ownership is chosen once and remembered. Running or
debugging another project from its row is a one-time target.

**Build Context** contains Configuration, Target, Toolchain, Profile, and
Option overrides. Run selection is launch state, not a compiler dimension. One
compact status item shows the effective project and Configuration; its tooltip
includes the complete context, selection source, and current operation.

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

Compile-database-backed C++ configuration and structured analyzer diagnostics
remain active. Project analyzers and formatters require workspace trust and one
explicit enable decision; dependency locks are never bypassed.

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
development build and then `ngin` on `PATH`. Extension output defaults to
`.ngin/build/<product>/<Configuration>.<Target>.<Toolchain>`.

See the [editor protocol](../../docs/reference/editor-protocol.md),
[tooling guide](../../docs/guides/tooling.md), and
[CLI reference](../../docs/reference/cli.md).
