# NGIN Single-Project And VS Code Manifest Selection Plan

Status: Implemented

## Purpose

Make a single `.nginproj` a complete, convenient application authoring and
editor workflow without requiring a `.ngin` workspace manifest.

Also allow a user to explicitly select which `.ngin` or `.nginproj` is active
in a VS Code folder when automatic discovery chooses the wrong manifest. This
is particularly useful for repositories with multiple manifests and for manual
testing.

This is intentionally a narrow editor and selection improvement. It does not
change the V4 project model.

## Existing Contract

The V4 model already defines:

- `.nginproj` as one source product project
- `.ngin` as workspace policy and project grouping
- workspace context as optional for project-scoped CLI operations

The native CLI already loads a selected project directly and only applies a
workspace when one is discovered. Existing CLI inspection coverage exercises a
project without a `.ngin` manifest.

The main missing behavior is in the VS Code extension:

- discovery currently requires a `.ngin` manifest
- only projects declared by the discovered workspace are loaded
- active-editor inference takes precedence over the remembered project
- workspace-specific labels and commands are assumed throughout the UI
- the local project parser does not synthesize the CLI's implicit `dev`
  profile for a minimal project

## Goals

1. A folder containing only a `.nginproj` works as an NGIN project in VS Code.
2. Users can explicitly select an active `.ngin` or `.nginproj`.
3. A manual selection remains active until changed or reset.
4. Automatic discovery remains convenient when no manual selection exists.
5. VS Code and the CLI resolve the same selected project, profile, and
   workspace context.

## Non-Goals

- no new manifest type or schema
- no generated placeholder `.ngin`
- no V1, V2, or V3 compatibility behavior
- no package resolution redesign
- no general multi-workspace dashboard
- no custom recursive filesystem watcher
- no mode for forcing a project to ignore an existing ancestor workspace in
  the first implementation
- no changes to the one-project, one-product V4 contract

## Terminology

### Manifest Context

The selected authoring entry point:

- a `.ngin` workspace manifest, or
- a standalone `.nginproj`

### Standalone Project

A `.nginproj` used without an applicable `.ngin` workspace manifest.

### Manual Selection

A manifest or project explicitly chosen by the user. A manual selection is
pinned and must not be replaced by active-editor inference.

### Automatic Selection

A manifest or project inferred from the active document, workspace folder, or
the fact that only one candidate exists.

## Functional Requirements

### 1. Standalone Project Context

When a VS Code folder contains a `.nginproj` and no applicable `.ngin`:

- the extension must load the project
- the project directory is the NGIN context root
- the project appears in the NGIN view and status bar
- configure, build, clean, rebuild, run, debug, validate, inspect, graph, and
  applicable tooling commands use the selected project
- commands pass the explicit project path to the CLI
- default output remains `build/ngin/<Project>/<Profile>`
- no `.ngin` file is created

The CLI remains the source of truth for composition, packages, generation,
staging, launch, and tooling.

### 2. Manifest Selection Command

Add an editor command:

```text
NGIN: Select Manifest...
```

The command lists `.ngin` and `.nginproj` candidates in the current VS Code
folder. Each entry should clearly show:

- manifest name
- manifest kind: `Workspace` or `Project`
- path relative to the VS Code folder

Selecting a `.ngin`:

1. activates that workspace manifest
2. loads its declared projects
3. retains the current project if it belongs to that workspace
4. otherwise selects the sole project or prompts for a project

Selecting a `.nginproj` activates that project directly. If normal CLI
workspace discovery finds an applicable ancestor workspace, its policy may
still participate. Selecting a project does not imply a new
`--no-workspace` behavior.

The picker must include an `Auto-detect` entry that clears the manual manifest
and project pin.

The existing `NGIN: Select Project` command remains useful for choosing among
projects inside the active workspace context.

### 3. Selection Precedence

Project and context resolution must use this precedence:

1. explicit command or tree-item target
2. manually pinned manifest and project
3. project containing the active editor document
4. sole discovered candidate
5. interactive prompt, when the command permits prompting

Changing the active editor must not override a manual selection.

An explicit tree-item action may target a different project for that action
without silently changing the persistent pin unless the action is specifically
`Set Active Project`.

### 4. Persistent State

Persist:

- selected manifest path
- selected manifest kind
- selected project path
- selected profile per project

Selection state must be scoped by VS Code workspace folder. Multi-root folders
must not share one unqualified `lastProject` value.

Stored selections that no longer exist or no longer belong to the selected
context must be ignored and fall back to automatic discovery.

### 5. Default Profile

The VS Code project model must match the CLI for minimal projects:

- absent `DefaultProfile` means `dev`
- if the effective default profile is not explicitly declared, expose a
  minimal synthetic profile entry with that name
- authored profiles and an authored `DefaultProfile` retain precedence

This is editor-side selection metadata only. The CLI still owns the effective
profile contents.

### 6. Standalone UI

The extension must represent standalone projects truthfully:

- label the top-level context as a project rather than an authored workspace
- keep project, profile, build, run, debug, inspect, and graph actions
- hide or disable workspace-only operations such as `workspace status` and
  `workspace doctor`
- replace errors that require a `.ngin workspace manifest` with errors that
  refer to an NGIN manifest or active NGIN project

The Composition Graph may continue to report `"workspace": null` for a
standalone project.

### 7. Exact Workspace Selection

Selecting a particular `.ngin` must affect CLI resolution, not only the VS Code
display.

The CLI currently rediscovers a workspace from the selected project's ancestor
directories. That is ambiguous when:

- multiple `.ngin` files are in the same directory
- a workspace groups a project outside the workspace directory tree
- the editor explicitly selects a workspace other than the automatically
  discovered workspace

Add one narrow project-command override:

```text
--workspace <file.ngin>
```

When supplied:

- load exactly that V4 workspace manifest
- apply it to the selected project invocation
- report a clear diagnostic if the file is missing, invalid, or not a V4
  workspace
- use it consistently for project-scoped commands invoked by the extension

When absent, preserve current automatic workspace discovery.

This plan does not add `--no-workspace`.

## Discovery Boundaries

Manifest selection should search within the current VS Code workspace folder
and use VS Code's normal file-search exclusions. Generated and dependency
output directories such as `build`, `.ngin`, and `node_modules` should not be
offered as authored manifest candidates.

Discovery should not scan outside the opened folder. Explicit paths passed by
commands remain valid when already in scope.

## Likely Implementation Areas

### VS Code Extension

- `Tools/NGIN.VSCode/src/core/discovery.ts`
  - discover workspace and standalone project contexts
  - load a standalone project context
- `Tools/NGIN.VSCode/src/state/workspaceState.ts`
  - store manifest kind and path
  - scope state by workspace folder
  - enforce manual-selection precedence
- `Tools/NGIN.VSCode/src/core/xml.ts`
  - align default-profile metadata with the CLI
- `Tools/NGIN.VSCode/src/extension.ts`
  - register the manifest picker
  - pass an explicit workspace path when applicable
  - update errors and workspace-only command guards
- `Tools/NGIN.VSCode/src/ui/models.ts`
- `Tools/NGIN.VSCode/src/ui/sidebar.ts`
- `Tools/NGIN.VSCode/package.json`
- `Tools/NGIN.VSCode/README.md`

### Native CLI

- argument parsing for `--workspace`
- centralized project invocation resolution
- project-scoped command forwarding where commands bypass centralized
  invocation resolution
- `docs/specs/006-cli-contract.md`
- focused CLI tests

No manifest schema changes are required.

## Test Scope

### VS Code Unit Tests

- discover a folder containing only one `.nginproj`
- load a minimal project with the implicit `dev` profile
- discover multiple `.ngin` and `.nginproj` candidates
- ignore generated-output candidates
- retain a manual selection when the active editor changes
- clear a manual selection with `Auto-detect`
- discard a stored selection whose file was removed
- keep state separate between workspace folders
- render standalone context labels without workspace-only actions

### VS Code Integration Tests

- open a project-only folder and resolve an active project/profile
- run a project command with an explicit `--project`
- select a workspace manifest and verify the command includes the matching
  `--workspace`

### CLI Tests

- `--project` without a workspace continues to resolve successfully
- `--workspace <file>` loads the exact selected workspace
- automatic discovery is unchanged without `--workspace`
- invalid explicit workspace paths produce a focused diagnostic
- Composition Graph workspace identity matches the explicitly selected
  manifest

## Acceptance Criteria

### Standalone Project

Given a folder containing:

```text
Hello.Native.nginproj
src/main.cpp
```

and no `.ngin`, opening the folder in VS Code must:

- show `Hello.Native`
- select profile `dev` when no other default is authored
- allow validate, configure, build, run, and debug through the normal extension
  commands
- produce the same Composition Graph as the equivalent explicit CLI command

### Manual Project Pin

Given two projects in one VS Code folder:

- manually selecting project B keeps project B active
- opening or editing a file owned by project A does not replace the pin
- choosing `Auto-detect` allows active-editor inference to select project A

### Manual Workspace Selection

Given two workspace manifests:

- the manifest picker shows both with distinguishable paths
- selecting workspace B loads workspace B's project list
- extension CLI invocations identify workspace B explicitly
- graph inspection reports workspace B

## Suggested Delivery Order

1. Add standalone project discovery and implicit `dev` profile metadata.
2. Add pinned manifest/project state and the manifest picker.
3. Update standalone UI labels, errors, and workspace-only actions.
4. Add the exact CLI workspace override and pass it from the extension.
5. Add focused tests and update extension and CLI documentation.

These are implementation steps for one feature, not separate architecture
workstreams.

## Implementation Result

The feature is implemented as a narrow CLI and VS Code extension change:

- project-scoped CLI commands accept an exact `--workspace <file.ngin>`
  override while retaining ancestor discovery when it is absent
- a folder containing only a `.nginproj` resolves as a standalone editor
  context with the implicit `dev` profile
- `NGIN: Select Manifest...` pins either manifest kind per VS Code workspace
  folder and includes `Auto-detect` to clear the pin
- manual project selection outranks active-editor inference
- workspace-only tree actions are not shown for standalone project contexts
- project commands and generated tasks carry the exact selected workspace when
  one is active
- candidate discovery uses VS Code file search and rejects authored candidates
  under `build`, `.ngin`, and `node_modules`

Focused CLI and VS Code unit coverage records standalone resolution, exact
workspace identity, missing-workspace diagnostics, implicit profile metadata,
selection precedence, candidate filtering, and standalone UI presentation.

## Documentation Follow-Up

Completed: `Tools/README.md` now documents the active
`build/ngin/<Project>/<Profile>` default consistently with the CLI contract,
implementation ledger, and VS Code extension.
