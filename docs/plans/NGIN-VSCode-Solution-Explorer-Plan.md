# NGIN VS Code Solution Explorer Plan

Status: Implemented

## Summary

The NGIN VS Code sidebar should become the primary project-navigation surface
for an NGIN workspace, not only a compact resolved-state inspector.

The finished experience should combine two related views in the existing NGIN
activity-bar container:

```text
NGIN: SOLUTION
  workspace
    projects
      manifest
      project files
      dependencies

NGIN: ACTIVE PROJECT
  tooling
  launch
  publish
  generated inputs
  artifacts
  problems
```

The Solution view answers:

- Which products belong to this workspace?
- Which files can I edit?
- Which files are selected by the active NGIN profile?
- Which dependencies does this product use?
- Where is a selected file declared, generated, or excluded?

The Active Project view answers:

- What does the selected product/profile resolve to?
- Which tool runs, launches, publish targets, generated inputs, artifacts, and
  diagnostics are currently effective?

Physical file discovery is editor-local navigation data. Semantic file
membership, ownership, generated state, and provenance come from the NGIN
Composition Graph. The extension must not implement a second version of NGIN
glob expansion, profile selectors, overlays, project closure, or generator
ownership.

## Product Decisions

### Two Native Views

Use two native VS Code Tree Views inside the existing NGIN view container.
Do not use a webview.

The current `nginWorkspace` view ID remains stable and its displayed name
changes from `Workspace` to `Solution`. Keeping the ID preserves contributed
menu wiring and as much VS Code view state as possible.

A new `nginActiveProject` view contains the selected profile's resolved
operational state. It is visible but collapsed by default the first time a
workspace is opened. Users can move, resize, hide, or reorder either view using
normal VS Code view behavior.

### Files Are A First-Class Project Surface

Every workspace project can expose its physical project directory without
requiring that project to be active and without invoking the CLI.

The project manifest remains a dedicated first child. Remaining physical
entries appear under `Project Files` so authored files cannot collide visually
with virtual nodes such as Dependencies.

The default file mode is `All Project Files`:

- show authored physical files under the project root
- exclude known generated, cache, dependency, and output roots
- include useful dotfiles such as `.clang-format`, `.clang-tidy`, and
  `.gitignore`
- decorate graph-selected membership when reliable graph data is available

An `NGIN Inputs Only` toggle filters the active project to graph-selected
authored inputs. The toggle is persisted per VS Code workspace. In this mode,
an inactive project's Project Files group shows `Activate project to resolve
inputs` rather than making profile-specific claims or invoking inspect for
every project. Activating the row applies the project's stored/default profile
and replaces that message with the filtered file tree.

### Physical Presence And NGIN Membership Stay Distinct

The UI must not imply that every physical file participates in the selected
composition.

```text
Physical file
  present below a project root

Selected input
  selected by the active product/profile Composition Graph

Generated input
  declared or produced by a graph generator

Artifact
  build, stage, launch, publish, or tool output
```

Selected authored files use normal file presentation. Unselected physical files
remain visible in `All Project Files` mode with a subtle `not selected by
<Profile>` decoration. Generated inputs and artifacts are never mixed into the
authored physical tree.

When graph data is unavailable or stale, the UI reports membership as unknown.
It must not mark files as excluded based on incomplete information.

### The CLI Owns Semantic Membership

The V4 graph already owns resolved inputs but does not expose a complete
editor-oriented materialized file index for every directory-style source root.
Add an editor plan slice to the graph rather than recreating the resolver in
TypeScript.

The additive graph shape should be:

```json
{
  "facets": ["editor"],
  "plans": {
    "editor": {
      "projectRoot": "F:/repo/App",
      "files": [
        {
          "path": "src/main.cpp",
          "absolutePath": "F:/repo/App/src/main.cpp",
          "kind": "Source",
          "role": "Source",
          "ownerKind": "project",
          "ownerName": "App",
          "generated": false,
          "exists": true,
          "manifestPath": "F:/repo/App/App.nginproj",
          "explainIdentity": "source:src/main.cpp",
          "provenance": {
            "sourceKind": "project",
            "sourceName": "App",
            "manifestPath": "F:/repo/App/App.nginproj"
          }
        }
      ]
    }
  }
}
```

Required editor-file fields:

- project-relative `path` when the file is inside the project root
- normalized `absolutePath`
- input `kind` and `role`
- `ownerKind` and `ownerName`
- `generated`
- current existence state
- declaring `manifestPath`
- stable explain identity when one exists
- provenance

The slice contains selected files only. The extension discovers other physical
files and computes `unselected = physical - selected` for presentation. An
input outside the project root remains selected but is shown under `External
Inputs`; it is not traversed as part of the physical project tree.

Directory and glob declarations must be materialized using resolver-owned
matching rules. Generated declarations may be present before their outputs
exist. Package and referenced-project inputs retain their owner identity so the
extension can avoid presenting dependency files as if the active product owned
them.

This is an additive V4 graph change. Update the frozen graph spec, schema,
metadata output, C++ graph model, JSON writers, TypeScript types, and focused
tests together. Do not change project-manifest syntax for this work.

## Current Baseline And Gaps

The current extension already has useful foundations:

- project, manifest, dependency, inspect, folder, and file TreeItem classes
- lazy one-directory `readdir` behavior for folder rows
- file resource URIs and open/reveal/copy-path commands
- create source/config file, create folder, copy, duplicate, rename, and
  trash-delete commands
- active-project-only inspect caching
- graph-native dependencies, tooling, launch, publish, artifact, and problem
  models
- persisted manifest, project, and profile selection

The missing or incomplete pieces are:

- no project-files group is inserted by the presentation model, so the existing
  physical file rows are unreachable from the normal tree
- the current reader hides all dotfiles, including useful project configuration
- file changes outside extension commands are not reflected by a dedicated
  project-tree watcher
- every sidebar refresh replaces the whole tree rather than invalidating the
  affected branch
- there is no stable parent/ID model for reveal-active-file behavior
- generic source creation does not verify or author NGIN membership
- manifest-aware filesystem operations update config paths only and use a
  config-specific regex service
- generated, external, selected, excluded, unknown, and project-boundary states
  are not modeled as distinct file concepts
- all semantic groups and future physical files would compete for space in one
  view

Implementation should reuse the working foundations while replacing these
gaps. It should not create a parallel third sidebar provider or retain the
one-view presentation as a compatibility mode.

## Target Information Architecture

### Workspace Context

For a multi-project workspace:

```text
NGIN: SOLUTION
└─ NGIN                                      <workspace>
   ├─ NGIN.CLI                              Tool
   ├─ Hello.Native                          Application
   ├─ Hello.Hosted                          Application
   └─ NGIN.UI.Gallery.Hosted                active · Debug
      ├─ Manifest                           NGIN.UI.Gallery.Hosted.nginproj
      ├─ Project Files
      │  ├─ .clang-format
      │  ├─ assets
      │  ├─ include
      │  └─ src
      └─ Dependencies                       8
         ├─ Project References
         ├─ Direct
         └─ Transitive
```

For a standalone `.nginproj`, avoid a duplicate workspace/project pair:

```text
NGIN: SOLUTION
└─ MyApp                                    active · Debug
   ├─ Manifest                              MyApp.nginproj
   ├─ Project Files
   └─ Dependencies
```

The full filesystem path belongs in the tooltip. Do not use a long absolute
path as the normal workspace-row description.

### Active Project Context

The Active Project view omits another project wrapper. Its view description
shows `<Project> · <Profile>` and its root children are:

```text
NGIN: ACTIVE PROJECT             NGIN.UI.Gallery.Hosted · Debug
├─ Tooling
├─ Launch
├─ Publish
├─ Generated Inputs
├─ Artifacts
└─ Problems
```

Empty groups stay hidden, matching the current compact behavior. Problems
remain last. Generated Inputs contains graph-declared generated sources,
headers, content, and reports that are useful to open; Artifacts remains the
place for executable, stage directory, launch manifest, compile database, and
publish outputs.

Dependencies stays in Solution because it is part of project navigation.
Tooling, Launch, Publish, Artifacts, and Problems move to Active Project because
they are profile-resolved operational state.

### Nested Projects

Project directory trees can overlap. When a physical tree reaches the root of
another workspace project, emit a project-boundary row instead of recursively
duplicating that project's files:

```text
Project Files
└─ tools
   └─ AssetCompiler              project · open in solution
```

Activating the boundary selects and reveals the corresponding project.

### External And Generated Inputs

Selected inputs outside the active project root appear as flat or
root-grouped virtual paths:

```text
External Inputs
└─ SharedAssets
   └─ common.json

Generated Inputs
└─ reflection
   ├─ Foo.generated.cpp
   └─ Foo.generated.hpp          not generated yet
```

External inputs are openable and revealable but read-only from NGIN Solution
commands by default. Generated inputs never expose rename, move, delete, or new
file actions.

## View Actions

### Solution Title

Keep no more than three frequent inline actions:

1. `Select Manifest`
2. `Toggle NGIN Inputs Only`
3. `Refresh`

Collapse All remains supplied by `createTreeView`.

Secondary title actions:

- Follow Active Editor
- Reveal Active File
- Workspace Status
- Workspace Doctor
- Configure Solution Explorer

Build and run actions move to the Active Project title and remain available on
project context menus, the Command Palette, tasks, and status bar.

### Active Project Title

Frequent inline actions:

1. Build
2. Run
3. Select Profile

Secondary actions:

- Configure
- Rebuild
- Clean
- Debug
- Validate
- Analyze
- Publish
- Graph
- Show Resolved Inputs
- Show Inactive Tooling

### Project Context Menu

- Set Active Project
- Build
- Run
- Debug
- Validate
- New Source File
- New Config File
- New File
- New Folder
- Open Manifest
- Open Manifest XML Source
- Reveal Project Directory
- Copy Project Path

### File And Folder Context Menu

Common authored operations:

- Open
- New File
- New Source File
- New Config File
- New Folder
- Rename
- Duplicate
- Delete to Trash
- Copy Relative Path
- Copy Absolute Path
- Reveal in Operating System
- Reveal in VS Code Explorer

NGIN-aware operations:

- Explain Membership
- Open Declaring Manifest
- Include in Product
- Exclude from Product

Only show commands that are valid for the row's context value. Never offer
mutation commands on generated nodes, dependency-owned inputs, artifacts, or
external paths.

## File Presentation

### Icons

Set a resource URI whose path preserves the real file name so the active VS
Code file icon theme can choose language-specific icons. Do not force every
authored file to the generic `file` ThemeIcon.

Virtual nodes continue to use product icons:

- workspace: `folder-library`
- project: `project` or active `target`
- manifest: `file-code`
- dependencies: `references`
- external inputs: `link-external`
- generated inputs: `sparkle` or `files`
- artifacts: `archive`
- problems: severity-appropriate icons

### Decorations

Use a private `ngin-solution:` resource URI for Solution-specific file
decorations so a membership badge does not leak into VS Code's normal Explorer
for the same `file:` URI.

Decoration states:

- unselected physical file: muted color and tooltip
- generated but missing: warning color and `not generated yet`
- selected external input: link badge and ownership tooltip
- graph diagnostic attached to a file: severity color
- stale/unknown membership: no exclusion color; tooltip says graph unavailable

Avoid badges on every selected file. The absence of a warning/exclusion
decoration is the normal state.

### Tooltips

Authored-file tooltips include:

- absolute path
- active profile
- membership: selected, unselected, or unknown
- NGIN kind/role when selected
- owner
- declaring manifest
- provenance summary

Keep normal labels short. Put detailed graph information in the tooltip and
Explain command rather than descriptions on every row.

### Sorting

Default ordering inside Project Files:

1. project boundaries
2. directories
3. files

Within each category, use case-insensitive natural sorting with a stable
case-sensitive tiebreaker. Manifest, Project Files, and Dependencies keep fixed
semantic ordering at the project level.

Optional compact-folder rendering may collapse one-child directory chains, but
it must not cross project boundaries or directories that carry distinct NGIN
membership state.

## File Discovery And Exclusion

### Root

The physical root of a project is `ProjectManifest.directory`. The manifest is
shown once as the dedicated Manifest child and omitted from Project Files.

### Hard Exclusions

Never enumerate these as authored Project Files:

- `.git`
- `.ngin`
- `node_modules`
- the resolved workspace output root
- the resolved active-project output directory
- configured build trees that are descendants of the project root

Do not hard-exclude every directory named `build` without checking ownership
and resolved output policy; a project may intentionally contain authored data
under that name. The default workspace output root can be excluded when its
resolved path is known.

### Configurable Exclusions

Add `ngin.solutionExplorer.exclude`, using the same glob vocabulary across
platforms. Merge it with applicable boolean `files.exclude` entries where they
can be evaluated safely.

Do not treat `search.exclude` as a navigation exclusion by default.

Useful dotfiles remain visible unless explicitly excluded.

### Links And Special Files

- show symbolic links as leaf/link rows by default
- do not recurse through directory symlinks
- detect and stop cycles
- do not treat sockets, devices, or other special filesystem objects as normal
  files
- retain case-correct display paths while using platform-correct comparable
  keys

### Error Rows

Permission failures or transient filesystem errors produce a small retryable
error row under the affected folder. An unreadable folder must not empty or
invalidate the whole solution tree.

## State And Selection

### Active Project

Preserve current explicit project/profile selection precedence. Expanding a
project does not silently change the active project. Activating a project row,
using Set Active Project, or opening a file under an unpinned project may
change selection according to the existing selection policy.

The active row shows:

```text
active · <Profile> · <N problems>
```

Inactive rows may show product kind but no profile-specific claims.

### Follow Active Editor

Provide a persisted `Follow Active Editor` toggle, off by default.

When enabled:

1. map the active `file:` document to the most specific workspace project
   boundary
2. activate that project only when current selection is not explicitly pinned
3. reveal the file in Solution
4. do not steal focus from the editor

When disabled, `Reveal Active File in NGIN Solution` performs a one-shot
reveal.

Implement stable `TreeItem.id` values and `TreeDataProvider.getParent` so
`TreeView.reveal` can expand the necessary ancestor chain.

### Persisted View State

Persist per VS Code workspace:

- all-files versus NGIN-inputs-only mode
- follow-active-editor state
- compact-folder state if exposed

Let VS Code own expanded/collapsed tree state. Stable IDs must include
workspace manifest identity, project manifest identity, node kind, and
normalized path without including transient profile labels.

## Filesystem Refresh Architecture

### Lazy Enumeration

Never recursively enumerate a project when the solution loads.

`getChildren(folder)` performs one directory read, applies exclusions, checks
project boundaries, sorts entries, and caches the result. NGIN Inputs Only mode
may synthesize the minimal ancestor folders from the membership index without
walking unrelated directories.

### Cache

Introduce a bounded `ProjectFileTreeService` with:

- directory child cache
- normalized project-boundary index
- active membership index
- negative stat cache with a short lifetime
- generation token per workspace snapshot

Changing project, profile, selected manifest, output root, exclusions, or file
mode invalidates only the affected cache layers.

### Watchers

Use VS Code filesystem events rather than polling.

- watch project roots using workspace-relative patterns
- filter events through project boundaries and exclusion policy
- subscribe to create, delete, rename, and save events
- debounce bursts
- invalidate the changed directory and relevant ancestors
- fire tree change events for the narrowest stable node
- refresh graph membership only when a manifest or selected/generated input
  change can affect resolution

Do not create one recursive operating-system watcher per folder row. Maintain
one watcher coordinator per VS Code workspace folder and route events to
project caches.

### Stale Async Results

Directory enumeration and inspect calls can finish after selection changes.
Every async result must carry a snapshot generation and be discarded if it no
longer matches the active manifest/project/profile.

## Authoring And File Operations

### Safety Boundary

All mutation commands resolve and verify:

- the owning project
- the absolute source and destination
- containment within the project root
- project-boundary crossings
- generated/external/read-only state
- destination collisions

Parent traversal, unresolved environment variables, and mutation outside the
project root are rejected. Delete remains recoverable through the system trash
when supported.

### New Files

Offer both generic and semantic creation:

- `New File` creates an authored physical file only
- `New Source File` creates the file and checks graph membership
- `New Config File` creates the file and offers the appropriate staged/config
  authoring change

After semantic creation:

1. determine whether the file is already covered by a selected directory or
   glob declaration
2. if already selected, make no manifest edit
3. otherwise offer `Add exact file to <Product>`
4. show the resulting manifest edit before applying when it changes staging
   target or visibility semantics

Do not rewrite a user's glob merely to include one new file.

### Rename And Move

Classify manifest impact before mutation:

- file covered by a directory/glob both before and after: no manifest edit
- exact file declaration: update that exact declaration
- renamed file falls outside a selected pattern: offer an explicit exact-file
  addition or cancel
- directory move affects exact descendants: preview the remap
- directory move affects a glob/root declaration: do not guess; open the
  declaration and require explicit confirmation

Use VS Code WorkspaceEdit-backed file and text edits where practical so editor
dirty state and undo behavior remain native. If trash semantics prevent a
single atomic edit, complete the recoverable filesystem operation first and
only then apply a validated manifest update.

### Delete

Before deleting a selected exact input, display the manifest impact. Directory
and glob declarations are not silently removed because one matching file is
deleted.

Deleting a folder reports:

- number of physical descendants
- selected exact inputs affected
- whether any nested project boundary would be crossed

Never recursively delete across another project's root.

### Include And Exclude

Phase one of authoring support uses exact-file entries because it is
deterministic and reviewable.

`Include in Product`:

- selects kind/role from the requested command or a small picker
- writes into the current V4 product section
- uses existing project-editor targeted edit infrastructure
- validates after editing

`Exclude from Product`:

- removes an exact local declaration when that declaration owns membership
- otherwise authors an explicit V4 exclusion only if the active manifest
  contract already supports the required scoped exclusion
- if membership comes from an inherited profile, workspace policy, package, or
  broad glob that cannot be safely overridden, open Explain and the declaring
  manifest instead of inventing syntax

Do not add legacy root-level manifest sections or editor-only exclusion files.

### Typed Manifest Editing

Replace the current config-only regex mutation path with a typed project input
authoring service shared with the visual project editor.

The service should:

- understand the active product kind
- preserve comments, whitespace, and unsupported XML
- create targeted VS Code text edits
- support exact Source, Header, Config, Content, and Asset entries
- update exact paths on rename
- remove exact entries on delete
- return a structured impact/result model for preview and tests

The extension may author valid XML, but semantic selection after the edit is
confirmed by a fresh CLI inspect result.

## Multi-Selection And Drag And Drop

Multi-selection is useful for open, reveal, copy path, delete, include, and
exclude. Enable it only after single-item safety and manifest-impact behavior
are complete.

Internal drag and drop is a final workstream:

- files/folders may move only within the same project root
- dropping across a project boundary is rejected
- generated, dependency, and external rows cannot be dragged
- manifest impact is previewed before applying
- operating-system file drops into a project are treated as copy operations
  only after explicit confirmation

Drag and drop is not required for the first usable Solution Explorer milestone,
but it is included in the fully fledged completion target.

## Accessibility And Keyboard Behavior

- every icon-only action has a descriptive command title and tooltip
- row labels and descriptions remain meaningful without color or badges
- file state is present in accessible tooltip/information text
- normal arrow-key expansion and list navigation work
- Enter opens files and activates virtual targets
- F2 invokes Rename for mutable authored paths
- Delete invokes the confirmed trash operation
- context menus contain the same actions available to mouse users
- reveal operations do not move editor focus unexpectedly

Do not depend on hover-only inline actions for essential operations.

## Empty, Degraded, And Error States

### No Manifest

Use `viewsWelcome` with:

- Select Manifest
- Create NGIN Project
- documentation link

### CLI Missing

Solution physical browsing and safe file operations remain usable. Active
Project shows a concise CLI-unavailable message. Membership is unknown, not
excluded.

### Inspect Failure

Keep the last successful membership snapshot only if it is visibly marked
stale. Otherwise show physical files without membership claims. Surface the
inspect diagnostic in Active Project Problems and provide Retry.

### Missing Selected File

An editor-plan file that does not exist appears in Generated Inputs if
generated, or Problems if authored and required. Do not synthesize a normal
physical file row.

### Empty Project

Project Files shows a welcome-style child with New Source File and New Folder
actions rather than an unexplained empty expansion.

## Implementation Architecture

### Extension Modules

Refactor the current broad sidebar implementation into:

```text
src/ui/
  solutionTree.ts
  activeProjectTree.ts
  projectFiles.ts
  fileDecorations.ts
  models.ts
  sidebar.ts
```

Responsibilities:

- `solutionTree.ts`: workspace/project/manifest/files/dependencies provider
- `activeProjectTree.ts`: selected-profile semantic provider
- `projectFiles.ts`: enumeration, exclusions, boundaries, cache, membership
- `fileDecorations.ts`: private-scheme decorations and tooltips
- `models.ts`: pure presentation models and stable IDs
- `sidebar.ts`: two-view controller, event routing, disposal, reveal API

Keep model-building functions independent of VS Code classes where possible so
unit tests can exercise ordering and state without an Extension Host.

### State

Extend the sidebar-facing snapshot with:

- graph freshness
- editor file plan
- resolved project/output roots
- view-state inputs only when needed for pure model generation

Keep filesystem caches out of `NginWorkspaceSnapshot`; they belong to the
project-file service and are keyed by manifest/project/profile generation.

### Graph Cache

Continue caching inspect by selected project/profile/output context. Include
the selected workspace manifest identity in the cache key. A manifest save,
profile change, graph-affecting setting change, or explicit Refresh invalidates
the matching entry.

Physical directory refresh must not force a CLI inspect call unless semantic
membership may have changed.

### Commands And Context Values

Replace role-only context values with explicit dimensions encoded in stable
context-value variants:

- authored file/folder
- selected/unselected/unknown
- mutable/read-only
- generated
- external
- project boundary
- manifest

Keep command targets as typed objects. Do not infer ownership only from the
active editor when a tree item already supplies project and path identity.

## Repository Change Map

### CLI And Graph Contract

- `Tools/NGIN.CLI/src/Model.hpp`
  - add graph editor-file plan structures
- `Tools/NGIN.CLI/src/Resolution.cpp`
  - reuse resolver matching to materialize editor-owned files
- `Tools/NGIN.CLI/src/Commands.cpp`
  - populate and serialize the editor plan
- `Tools/NGIN.CLI/tests/GraphInspectTests.cpp`
  - cover File, Directory, Glob, include/exclude, profile, generated, external,
    owner, provenance, and missing-output cases
- `docs/specs/013-composition-graph-json-contract.md`
  - document the additive editor plan
- `docs/schemas/ngin-composition-graph-v4.schema.json`
  - validate the editor plan

### VS Code Extension

- `Tools/NGIN.VSCode/package.json`
  - rename Solution view, add Active Project view, actions, menus, settings,
    welcome content, and keybindings where appropriate
- `Tools/NGIN.VSCode/src/core/types.ts`
  - add editor plan payload types
- `Tools/NGIN.VSCode/src/ui/sidebar.ts`
  - coordinate the two providers and reveal behavior
- `Tools/NGIN.VSCode/src/ui/models.ts`
  - split solution and active-project pure models
- new `Tools/NGIN.VSCode/src/ui/projectFiles.ts`
  - implement lazy physical navigation and membership indexing
- new `Tools/NGIN.VSCode/src/ui/fileDecorations.ts`
  - implement private Solution decorations
- `Tools/NGIN.VSCode/src/state/workspaceState.ts`
  - respond to Solution settings and expose stable selection/freshness data
- `Tools/NGIN.VSCode/src/core/projectAuthoring.ts`
  - replace config-only mutations with typed exact-input operations
- `Tools/NGIN.VSCode/src/projectEditor/authoring.ts`
  - share targeted V4 input edit primitives
- `Tools/NGIN.VSCode/src/extension.ts`
  - register commands, watcher coordinator, manifest-impact flows, and reveal
- `Tools/NGIN.VSCode/src/test/unit/`
  - split the monolithic unit coverage into focused files if test
    infrastructure permits without duplicating helpers
- `Tools/NGIN.VSCode/src/test/integration/suite/extension.test.ts`
  - add temporary-workspace interaction coverage
- `Tools/NGIN.VSCode/README.md`
  - document the Solution and Active Project workflows
- `Tools/README.md`
  - update the extension capability summary

No generated file under `build/` is an implementation target.

## Delivery Workstreams

### Workstream A: Freeze UX And Graph Contract

Deliverables:

- final tree names and hierarchy
- final active/inactive/membership state vocabulary
- editor plan JSON shape
- exclusion and project-boundary rules
- command/context matrix
- representative screenshots or textual fixtures for workspace, standalone,
  generated, external, missing CLI, and inspect-error states

Acceptance:

- each displayed fact has an identified owner: filesystem, authored parser,
  workspace state, Composition Graph, or built artifact
- no semantic membership rule is assigned to TypeScript

### Workstream B: Split The Existing Sidebar

Deliverables:

- stable `nginWorkspace` ID displayed as Solution
- new `nginActiveProject` view
- Dependencies retained in Solution
- Tooling, Launch, Publish, Artifacts, and Problems moved to Active Project
- existing commands and resolved detail behavior preserved

Acceptance:

- the current sidebar test expectations are divided between the two models
- project/profile selection and toolbar actions still work
- no file browsing is required yet

### Workstream C: Physical Project Files

Deliverables:

- Project Files group for every project
- lazy directory enumeration
- useful dotfiles
- hard/configured exclusions
- nested-project boundaries
- symlink and error handling
- correct icons, sorting, stable IDs, and getParent
- narrow watcher invalidation

Acceptance:

- browsing files requires no CLI
- expanding one folder never scans the whole repository
- generated output roots do not flood the tree
- physical changes appear without a full manual refresh

### Workstream D: Safe Basic File Operations

Deliverables:

- generic New File/New Folder
- open, rename, duplicate, trash delete, copy path, and reveal
- containment and boundary validation
- authored/generated/external context restrictions
- single-item keyboard behavior

Acceptance:

- temporary integration workspaces cover every mutation
- no test mutates authored repository examples
- generated, external, and cross-project destructive operations are rejected

### Workstream E: Graph-Owned Editor File Plan

Deliverables:

- C++ graph/editor plan model
- resolver-owned materialization
- JSON writer/schema/spec
- TypeScript payload model
- inspect cache integration

Acceptance:

- File, Directory, and Glob inputs resolve to the same files used by NGIN
- include/exclude and profile selectors are reflected
- generated, external, and project-reference ownership is explicit
- graph JSON remains backward-compatible through additive fields

### Workstream F: Membership UI

Deliverables:

- active membership index
- All Project Files and NGIN Inputs Only modes
- unselected, unknown, generated, external, and missing decorations
- detailed tooltips
- Explain Membership and Open Declaring Manifest
- profile-change invalidation

Acceptance:

- switching profiles changes membership without restarting VS Code
- inspect failure never produces false exclusion claims
- dependency-owned inputs are not shown as active-project authored files

### Workstream G: Manifest-Aware Authoring

Deliverables:

- typed exact-input edit service
- semantic source/config creation
- include/exclude exact file
- exact-path rename/delete updates
- manifest impact preview
- validation and graph refresh after authoring

Acceptance:

- no legacy manifest section is created
- no broad glob is silently rewritten
- comments and unsupported XML survive
- the refreshed Composition Graph confirms the intended membership

### Workstream H: Follow, Multi-Select, And Drag And Drop

Deliverables:

- one-shot Reveal Active File
- persisted Follow Active Editor
- multi-selection for safe batch actions
- internal move/copy drag and drop with impact preview
- stale-result cancellation

Acceptance:

- explicit pinned selection is respected
- reveal does not steal editor focus
- cross-project and generated/external moves are rejected

### Workstream I: Documentation, Performance, And Release Polish

Deliverables:

- extension and Tools README updates
- docs index entry
- accessibility audit
- large-workspace performance fixture
- Windows/Linux path and case-sensitivity verification
- release notes and screenshots

Acceptance:

- new users can discover file navigation without documentation
- no permanent compatibility path keeps the old one-view layout alive
- docs clearly distinguish physical files, selected inputs, generated inputs,
  and artifacts

## Recommended Delivery Sequence And Verification Gates

Deliver the workstreams in these reviewable slices:

1. Workstreams A and B: freeze the contract and split the views.
2. Workstreams C and D: ship useful physical browsing and safe basic file
   operations without waiting for a graph change.
3. Workstream E: add the graph-owned editor file plan.
4. Workstreams F and G: connect semantic membership and typed manifest
   authoring.
5. Workstreams H and I: add advanced interaction and release polish.

Do not hide incomplete membership behavior behind heuristics between slices.
Before Workstream E lands, physical browsing is complete but membership is
reported as unavailable.

Targeted verification by slice:

- view/model/file-service changes:
  `npm run typecheck` and `npm run test:unit` from `Tools/NGIN.VSCode`
- command, watcher, reveal, or filesystem interaction changes:
  add `npm run test:integration`
- graph editor-plan changes:
  build `ngin_cli` and `NGINCliTests`, then run the focused CLI test executable
- schema/spec-only changes:
  run the existing schema parse/contract test rather than a workspace build
- final integrated milestone:
  run extension typecheck, unit tests, integration tests, and one manual
  Hello.Native/Hello.Hosted Solution Explorer smoke pass

Escalate to broader workspace tests only if shared CLI resolution behavior
changes or the targeted graph tests expose wider regression risk.

## Test Strategy

### Pure Unit Tests

Cover:

- stable ID creation
- platform path comparison and containment
- project boundary selection, including nested projects
- hard and configurable exclusions
- useful dotfiles
- directory-first natural sorting
- compact-folder stopping rules
- membership indexing
- all-files versus inputs-only filtering
- unknown/stale graph behavior
- external and generated classification
- fixed project child ordering
- active-project group ordering
- manifest-impact classification
- typed exact-input authoring edits

### CLI Tests

Add focused cases to `GraphInspectTests.cpp`:

- exact source/header/config/content/asset
- directory root
- glob plus include/exclude
- product and profile additions/removals
- generated output before and after existence
- input outside project root
- project-reference and package ownership
- normalized paths on Windows and POSIX
- provenance and explain identity
- JSON schema acceptance

### Extension Host Integration Tests

Use temporary projects/workspaces to cover:

- both views are contributed
- standalone project has no duplicate wrapper
- project folder expansion and file opening
- file create/rename/duplicate/trash flow
- watcher-driven create/delete refresh
- profile switch updates membership
- input-only toggle persistence
- reveal active file
- pinned selection protection
- generated/external mutation rejection
- inspect failure degrades to physical browsing

Avoid timing-only assertions. Wait on documents, filesystem state, commands, or
provider-visible state.

### Manual Matrix

- Windows with case-insensitive paths
- Linux with case-sensitive paths
- standalone `.nginproj`
- multi-project `.ngin`
- nested project directories
- project outside the first workspace folder
- multiple VS Code workspace folders
- missing CLI
- malformed manifest
- missing generated output
- light/dark themes and multiple file icon themes
- keyboard-only navigation
- workspace with at least 10 projects and 10,000 physical files

## Performance Budgets

For a warm extension host on a representative local workspace:

- initial Solution project rows should render without recursive file scanning
- expanding an ordinary cached directory should feel immediate
- a single filesystem event should update the affected branch within one
  debounce interval, not rebuild every tree node
- switching profile may wait for one inspect call but physical browsing stays
  responsive
- caches remain bounded and are released when projects/workspace folders close

Record measurements before choosing fixed millisecond gates in CI. Do not add
flaky wall-clock unit tests.

## Non-Goals

- replacing VS Code Search, Source Control, Outline, or Problems
- showing every dependency package source tree as project files
- editing generated outputs or build artifacts
- inventing an editor-only project file or exclusion format
- adding V1/V2/V3 manifest compatibility
- redesigning V4 project authoring syntax
- eagerly resolving every project/profile graph at workspace load
- traversing arbitrary external input roots
- using a custom webview to imitate Visual Studio

## Risks And Mitigations

### Sidebar Becomes Too Dense

Mitigation: split Solution from Active Project, keep empty semantic groups
hidden, keep Project Files collapsed by default for inactive projects, and put
detail in tooltips/Explain.

### Extension Reimplements Resolution

Mitigation: add the graph editor plan and treat missing graph data as unknown.
Never infer selected/excluded membership from TypeScript glob matching.

### Large Workspaces Stall

Mitigation: lazy one-directory reads, bounded caches, narrow invalidation,
project boundaries, and no eager inspect for inactive projects.

### Decorations Pollute Normal Explorer

Mitigation: use private `ngin-solution:` resource URIs for NGIN-specific
decorations while retaining separate real file targets.

### Manifest Updates Corrupt Authoring

Mitigation: typed targeted edits, impact preview, exact-entry changes first,
validation after edits, and no broad-pattern guesses.

### Overlapping Projects Duplicate Or Delete Files

Mitigation: most-specific ownership, explicit project-boundary rows, and
containment checks before every mutation.

### Stale Inspect Results Lie

Mitigation: generation-tag async work, freshness state, cache invalidation by
manifest/project/profile, and unknown rather than excluded on failure.

## Definition Of Done

The Solution Explorer work is complete when:

- the NGIN container provides native Solution and Active Project views
- every project can browse authored physical files without the CLI
- standalone projects do not have redundant hierarchy
- known output/cache roots and nested projects are handled safely
- the Composition Graph emits materialized editor file membership
- active-profile selected, unselected, generated, external, missing, and
  unknown states are represented accurately
- Dependencies remains navigable in Solution
- Tooling, Launch, Publish, Generated Inputs, Artifacts, and Problems remain
  available for the active project
- file operations validate ownership and project boundaries
- semantic create/include/exclude/rename flows use typed V4 manifest edits
- no generated or external file can be destructively changed from the view
- profile and manifest changes refresh membership without restarting VS Code
- reveal/follow-active-editor works with pinned-selection semantics
- focused CLI, unit, and Extension Host tests pass
- extension documentation teaches the new workflow
- no legacy manifest compatibility or editor-only resolution path is added

At that point the NGIN sidebar is a genuine product-aware Solution Explorer:
useful for ordinary file navigation, but more precise than a filesystem-only
tree because it can explain what the selected NGIN product/profile actually
uses.
