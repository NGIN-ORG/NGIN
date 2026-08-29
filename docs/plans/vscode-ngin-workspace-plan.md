# NGIN Tools for VS Code — Overhauled Plan

## Status

Proposed, August 2026.

This plan starts from the product-first schema now implemented in NGIN and from
the current VS Code extension on `master`. It replaces the proposed “complete
work surface” plan where that plan would make the extension responsible for
NGIN semantics or commit too early to a large second file manager.

## Recommendation

Keep the product-scoped physical tree. It is the right differentiator for NGIN:
developers should be able to see the files belonging to an `Executable` or
`Library`, their build membership, generated outputs, and composition in one
place.

Do not, however, build the new experience as an increasingly large collection
of TypeScript tree nodes and XML edits. The CLI or a shared first-party library
must provide the editor model and plan semantic mutations. The extension should
own interaction, preview, application through VS Code APIs, and presentation.

The order of work should therefore be:

1. establish the editor/authoring contract;
2. split the current extension into testable feature boundaries;
3. replace the grouped Sources/Headers tree with a lazy product file tree;
4. add a small, reliable creation workflow;
5. add rename, move, delete, and drag-and-drop only after semantic planning is
   authoritative; and
6. add composition mutation after the daily source workflow is proven.

This is not a rejection of the current direction. It is a change in ownership,
scope, and sequencing.

## Current baseline and gaps

The current extension already has a strong native VS Code shell: discovery,
typed graph consumption, lifecycle commands, tasks, debugging, Testing,
diagnostics, C++ configuration, a compact status item, and narrow manifest
edits. The new schema is represented by direct `Executable` and `Library`
roots, and Test and Benchmark are graph registrations rather than product
kinds.

The next plan must account for these implementation facts:

- `src/extension.ts` is currently the command and orchestration center. At
  roughly 950 lines, adding every file and composition workflow there will make
  behavior harder to isolate and test.
- `src/ui/tree.ts` couples the product model directly to `vscode.TreeItem` and
  rebuilds the presentation from graph and filesystem state.
- `src/core/projectFiles.ts` recursively enumerates an entire product when the
  product is expanded. It has a safety limit, but it is not folder-lazy and is
  based on local Node filesystem APIs rather than VS Code workspace APIs. It
  also treats every physical file absent from `buildItems` as `unselected`,
  which incorrectly makes ordinary files such as `.env` and `README.md` look
  like failed build membership.
- `src/core/manifestEdits.ts` safely handles a useful set of exact edits, but it
  cannot decide the semantics of globs, `When` contributions, `Remove`,
  `Update`, cross-context membership, or package resolution.
- the public CLI supports project/package/action authoring but does not yet
  expose the file-operation planning contract required by the proposed UI.

The main risk is therefore not visual design. It is creating a second,
incomplete implementation of authored and effective NGIN behavior in the
extension.

## Product model used by the UI

The extension uses the implemented schema directly:

| Authored concept | VS Code presentation |
| --- | --- |
| `.ngin` | Workspace |
| `.nginproj` | Product manifest |
| `Executable` | Executable product |
| `Library Kind="Static\|Shared\|Interface\|Plugin"` | Library product with its library kind |
| `Test` | Test registration in VS Code Testing, not a product |
| `Benchmark` | Benchmark registration/action, not a product |
| `CxxModule` | C++ build item, not a product or runtime module |
| NGIN.Core runtime registration | Application code and startup behavior, not authored project structure |

Do not reintroduce Application, Tool, Test, Benchmark, or Module as project
kinds. A command-line tool is an Executable. A test or benchmark is an
Executable with registrations. A plugin is a Library with `Kind="Plugin"`.

## Interaction model

### One NGIN Workspace view

Use one primary **Workspace** view:

```text
NGIN: WORKSPACE                         [new] [locate] [filter] [refresh]

NGIN                                     Debug · host
  Hello.Native                           Executable · launch
    Composition                          1 package
    .env
    README.md
    config
      local.json
    include
      Hello
        App.hpp
    src
      main.cpp
    Generated                            0
    Issues                               0
  NGIN.Core                              Shared library
    Composition
    Core
      Runtime.hpp
      Runtime.cpp
```

For a single standalone `.nginproj`, show the product directly. For multiple
authored workspaces or multi-root VS Code workspaces, show one Workspace node
per discovered NGIN workspace. Loose projects go under **Standalone Products**;
do not invent an authored workspace.

The tree is product-scoped, not a replacement for the repository-wide native
Explorer. It should expose physical files inside product boundaries because
NGIN membership and provenance make that view materially different from a
generic filesystem browser.

Files beside the workspace manifest but outside every product boundary may
appear in a collapsed **Workspace Files** branch. Do not recursively reproduce
product directories there. Files outside an NGIN workspace or product remain
the responsibility of the native Explorer.

### Physical layout, semantic overlay

Show authored directories directly. Do not create permanent Sources, Headers,
Resources, or Product Files groups.

Show every ordinary physical file inside a product boundary by default. The
active Build Context adds semantic decoration where NGIN semantics apply:

| State | Presentation | Mutation policy |
| --- | --- | --- |
| Selected authored file | Normal icon; kind in tooltip | Editable |
| Ordinary product file | Normal native file icon; no membership warning | Editable |
| Plausible build input not selected | Restrained `Not in build` decoration | Open, include, rename, delete |
| Selected only in another context | Context badge/tooltip | Explain or switch context |
| Stage/action input | Input decoration and semantic role | Editable when workspace-owned |
| Generated output | Generated branch and sparkle | Read-only by default |
| External input | External branch and owner/path | Never moved implicitly |
| Missing selected input | Issues plus Problems diagnostic | Open selecting rule |
| Nested product boundary | Product boundary node | Navigate to nested product |
| Dependency-owned source | Package ownership decoration | Openable, not project-mutated |
| Ignored/output path | Hidden by default | Reveal only through explicit toggle |

`Not in build` applies to recognized C/C++ inputs and files that match an
explicit product convention or declared resource root. An arbitrary extension
is not automatically a Resource candidate. Absence from the Composition Graph
is not a warning for `.env`, Markdown, scripts, configuration, licenses, or
other ordinary support files.

**Show Ignored Files** reveals paths hidden by NGIN output/dependency rules and
applicable VS Code exclusion settings. It does not change authored or effective
state. Build output, generated output already represented under Generated,
dependency caches, `.git`, `.ngin`, and similar high-noise roots remain hidden
by default.

A broken Composition Graph must not make the physical tree disappear. Generic
navigation and file creation continue to work; semantic decorations and
actions show one actionable model issue.

Do not duplicate the product manifest as a normal child file. Double-click the
product or use **Open Product Manifest**.

### Authoring is layout-neutral

NGIN Tools must not require or privilege `src`, `include`, `Public`, `Private`,
or any other directory convention. These are common layouts, not NGIN
semantics:

```text
# Co-located
Renderer/Renderer.hpp
Renderer/Renderer.cpp

# Include/source split
include/Engine/Renderer.hpp
src/Renderer.cpp

# Public/private split
Source/Renderer/Public/Renderer.hpp
Source/Renderer/Private/Renderer.cpp

# Header-only
include/Math/Vector.hpp
```

Physical paths suggest authoring intent, but effective `Build` rules determine
kind, selection, and visibility. A header selected as `Visibility="Public"` is
public regardless of whether it lives in `include`, `src`, or another folder.

Creation chooses locations in this order:

1. preserve the folder explicitly selected by the user for the primary item;
2. use covering Build rules and their visibility for semantic defaults;
3. infer paired-file placement from nearby header/source pairs;
4. use the dominant existing product convention;
5. use a remembered non-semantic editor preference; and
6. ask the user when multiple materially different placements remain.

For a new empty product, the project-creation flow may offer **Co-located**,
**Include/source split**, **Public/private split**, and **Custom** starting
layouts. The choice creates directories, starter files, and ordinary Build
rules. It does not become a new manifest product kind or permanent layout
semantic.

Layout support by product is rule-driven:

| Product | Authoring behavior |
| --- | --- |
| Executable | Co-located, split, public/private, or custom |
| Static/Shared/Plugin Library | Co-located, split, public/private, or custom |
| Interface Library | Headers and supported modules; do not offer compiled Source |
| Executable with Test/Benchmark registrations | Same layout rules as any Executable |
| C++ module-based product | Co-located, dedicated module tree, or custom |

The extension proposes desired paths. The CLI validates their semantic result;
the CLI does not choose where the developer should place files.

### Active Product and Launch Product are different

Use two explicit concepts:

- **Active Product** is derived from the active file and is used by Build,
  Analyze, Format, and file-scoped commands. If ownership is ambiguous, ask
  once and remember the choice for that path/context.
- **Launch Product** is explicitly selected and persisted per NGIN workspace.
  F5 and Ctrl+F5 use it. Running or debugging another executable from its row
  is a one-time target and does not silently change it.

If there is no active-file owner, global Build falls back to the Launch Product
and then asks the user if no valid product exists. If the Launch Product loses
Run intent, clear it and explain why.

Navigation selection never changes either concept.

### Build Context and launch selection are separate

**Build Context** contains Configuration, Target, Toolchain, Profile, and
Option overrides. It controls graph resolution and build identity.

**Launch Selection** contains Launch Product and Run. A Profile may derive a
Run, but Run should not be presented as though it were a compiler/build
dimension. This avoids making libraries appear to have an irrelevant Run
setting.

Keep one compact status item showing the active product and Configuration. Its
tooltip contains the complete Build Context, Launch Product, current operation,
and source of product selection. Clicking it opens a grouped action picker.

### Commands stay native and object-specific

Use native Tree View, Quick Pick, Testing, Problems, Run and Debug, Output,
tasks, Settings, and editors. Do not introduce a dashboard webview.

At most Build, Run, and Debug appear inline on a product row, and only when
valid. Context menus target the clicked object. Command Palette commands use
the Active Product or ask for a target.

Prefer concrete creation commands in menus:

- New File
- New Folder
- New C++ Source File
- New C++ Header File
- New C++ Class
- New C++ Module
- New C++ Item…

**New File** and **New Folder** are physical operations. They create arbitrary
files such as `.env`, `README.md`, JSON, scripts, assets, or documentation and
do not touch a manifest. **New C++ Item…** is the semantic creation engine and
may request an authoring plan.

Existing ordinary files may expose **Include in Build…**, **Add to Stage…**, or
another role-specific command when the CLI contract supports that role. Do not
automatically classify or stage a generic file based only on its extension.
In particular, `.env` must never be automatically added as a Resource, Stage
input, diagnostic detail, or tooltip content. The extension may offer a
separate, explicit `.gitignore` action without assuming the answer.

## Authoring contract

### Required CLI/shared editor model

The overhaul requires CLI work, but not another schema redesign. Before
complete file management, add a versioned editor-facing contract owned by the
CLI and preferably implemented in a shared first-party C++ library used by the
CLI. Command names are deliberately not fixed here; the data contract matters
first.

It must provide:

1. **Workspace snapshot** — discovered workspaces, stable product identities,
   product boundaries, workspace choices, and diagnostics.
2. **Product snapshot** — effective product, Build Context, file membership,
   matched authored rules, provenance, generated/external inputs, packages,
   capabilities, actions, Runs, Tests, and Benchmarks. Path references from
   Build, Stage, Generate, Tooling, and other authored roles must be
   distinguishable so an apparently ordinary file is not renamed unsafely.
3. **Authoring plan** — filesystem operations, comment-preserving manifest
   text edits, semantic before/after state, diagnostics, affected products,
   refresh scope, and precondition hashes.

The request supplies user intent and desired locations. For example:

```json
{
  "intent": "CreateItems",
  "project": "Engine.nginproj",
  "context": {
    "configuration": "Debug",
    "target": "host",
    "toolchain": "clang"
  },
  "items": [
    {
      "path": "Source/Renderer/Public/Renderer.hpp",
      "kind": "Header",
      "visibility": "Public"
    },
    {
      "path": "Source/Renderer/Private/Renderer.cpp",
      "kind": "Source"
    }
  ]
}
```

The response states whether existing rules already cover each item, which
rules and contexts select it, and the smallest required manifest edits. It may
return no manifest edit at all. The CLI must never rewrite a manifest merely to
normalize it as part of an unrelated file operation.

Initial intents:

```text
CreateItems
IncludeItems
ExcludeItems
RenameItems
MoveItems
DeleteItems
AddPackage
ChangePackageRequirement
RemovePackage
```

Generic **New File** and **New Folder** do not use these intents and require no
CLI round trip. They are applied through VS Code filesystem edits. Rename,
move, and delete still consult the semantic planner because a generic-looking
file may be referenced by Stage, an Action, or another authored role.

Capability-provider selection can be added after its authored workspace scope
and conflict behavior are proven.

The planner must answer cases the extension cannot infer safely:

- an existing glob already includes the new file;
- membership comes from a `When` block and differs by Build Context;
- a `Remove`, `Exclude`, or `Update` rule controls the path;
- the same physical file participates in multiple products;
- a move crosses a product boundary;
- an open manifest changed after the plan was produced; or
- the target is generated, external, or dependency-owned.

The extension displays the plan and applies it with `WorkspaceEdit` where
possible so VS Code undo and dirty documents work normally. Preconditions
prevent applying a plan to stale text. No operation may silently save or
overwrite an unrelated dirty manifest.

This boundary is explicit:

| Responsibility | Owner |
| --- | --- |
| Enumerate and display all ordinary physical folders/files | Extension |
| Select/infer proposed paths | Extension |
| Remember editor-only layout preferences | Extension workspace state/settings |
| Resolve membership, visibility, conditions, path roles, and provenance | CLI/shared model |
| Produce minimal manifest edits | CLI/shared model |
| Preview and apply changes with VS Code Undo | Extension |

No layout preference enters Manifest IR or the Composition Graph. Only the
ordinary paths, typed Build rules, and visibility authored by the resulting
operation affect NGIN semantics.

### Temporary extension-owned edits

The existing narrow XML editor may remain for already-tested exact operations
during the transition. It must sit behind the same intent/plan interface and
must not gain new semantic heuristics. Remove it from semantic authority once
the first-party planner covers those intents.

## Extension architecture

Refactor before adding broad mutation features.

```text
CLI/editor protocol
        ↓
Workspace store and operation coordinator
        ↓
Feature services (files, authoring, lifecycle, composition)
        ↓
VS Code adapters (tree, commands, tasks, debug, testing, diagnostics)
```

### Core model

Use plain TypeScript data independent of `vscode.TreeItem`:

- `WorkspaceSnapshot`, `ProductSnapshot`, `ProductFileNode`;
- stable IDs based on workspace/product identity and normalized relative path;
- explicit loading, ready, degraded, busy, and stale states; and
- targeted change events rather than whole-tree invalidation.

### Operation coordinator

Centralize CLI execution, cancellation, locking, progress, output policy, and
cache invalidation. Cache graph/product snapshots by the complete effective
context and authored input versions. Coalesce identical requests.

One product operation may run at a time. Read-only requests should share cached
state and must not trigger Configure merely to populate the tree.

### Files feature

Use `vscode.workspace.fs` and folder-level lazy enumeration for local, remote,
and virtual workspaces. Do not recursively walk all product files on root
refresh. Reconcile filesystem watcher events into affected folders and retain
expansion, selection, and reveal state.

Keep physical existence separate from semantic classification. Merge the lazy
physical tree with CLI-provided path roles. Ordinary files remain ordinary;
only plausible build candidates receive `Not in build`, and only explicit
ignore/output rules hide paths.

### Authoring feature

Own item templates, input flows, preview, application, rollback reporting, and
targeted refresh. It consumes authoring plans; it does not parse NGIN selection
semantics.

### Composition feature

Initially present direct packages, transitive packages, capabilities, actions,
and generated outputs with provenance. Keep graph identities and edges in
Inspect, Explain, and JSON views. Mutation uses the same authoring service.

### Activation and command organization

Break the current activation file into feature registrars such as lifecycle,
workspace, authoring, composition, and manifest-language support. Preserve
stable public command IDs where behavior still matches; add aliases and one
deprecation cycle for renamed commands.

## Delivery plan

### Milestone 0 — Freeze UX and editor contracts

Deliver:

- approve Workspace, Product, Active Product, Launch Product, Build Context,
  and Launch Selection terminology;
- specify versioned snapshot and authoring-plan JSON contracts;
- add contract inputs for proposed path, build-item kind, visibility, and
  active Build Context without encoding a directory convention;
- define behavior for globs, `When`, cross-product files, dirty documents,
  stale plans, and broken graphs;
- record baseline timing and task-completion data using Hello.Native,
  Hello.Hosted, and Hello.Reflection; and
- decide which existing command IDs and settings are stable API.

Exit gate: contract fixtures cover selected, ordinary, plausible-not-selected,
conditional, staged/action input, generated, external, missing,
nested-boundary, ignored, and multi-owner files. No extension code needs to
guess semantic membership, and the same contract works for co-located,
include/source, public/private, header-only, and custom layouts.

### Milestone 1 — Refactor without changing the experience

Deliver:

- plain workspace/product/tree models;
- feature-based command registration;
- one operation coordinator;
- request coalescing and context-keyed caches;
- targeted model events; and
- characterization tests for current build, run, debug, test, analysis,
  manifest authoring, and project selection behavior.

Exit gate: the existing extension-host suite passes, `extension.ts` is an
activation/composition root rather than the implementation of every command,
and tree logic is unit-testable without VS Code.

### Milestone 2 — Read-only NGIN Workspace tree

Deliver:

- rename Projects to Workspace;
- add authored workspace and standalone-product roots;
- show all ordinary physical product files and folders directly;
- add a non-duplicating Workspace Files branch for unowned workspace-level
  support files;
- distinguish ordinary files from plausible build inputs that are not
  selected;
- add semantic overlays and Show Ignored Files;
- add Locate Active File;
- retain a collapsed Composition branch plus Generated and Issues branches;
- add folder-lazy enumeration and watcher reconciliation; and
- preserve current lifecycle, diagnostics, Testing, tasks, and debugging.

Exit gate: `.env`, README, scripts, configuration, sources, and headers are
visible without a mode switch; routine navigation no longer requires
Sources/Headers groups; a broken graph retains physical navigation; nested
products do not duplicate files; no root refresh recursively scans a product.

### Milestone 3 — Reliable creation

Start with Source File, Header File, Class, Module Interface, and Folder. Add
Struct, Enum, partitions, resources, batch import, and custom templates only
after the core flow proves useful.

Deliver:

- generic New File and New Folder through VS Code filesystem edits with no
  manifest mutation;
- layout-neutral destination and item-kind inference;
- support for co-located, include/source, public/private, header-only, and
  custom project layouts;
- explicit user selection whenever paired-file placement is ambiguous;
- optional namespace, visibility, include, and module details;
- preview when multiple files or any manifest edit is involved;
- atomic plan application with stale-document checks;
- immediate ownership, tree, diagnostic, and IntelliSense refresh; and
- aliases for existing New Source/New Header commands.

Exit gate: a file covered by a glob creates no redundant exact rule; class
creation produces a valid header/source pair in both co-located and split
layouts; an Interface Library never offers an invalid compiled Source;
creating `.env`, Markdown, JSON, scripts, and unknown extensions produces no
manifest edit; unsupported module contexts are explained; cancel/failure
leaves no partial state.

### Milestone 4 — Safe file management

Deliver in risk order:

1. Include and Exclude;
2. Rename and Move;
3. Delete with recoverable deletion;
4. Duplicate and Import; and
5. drag-and-drop and multi-selection.

Use the same authoring-plan path for tree, Explorer, editor, and Command
Palette entry points for semantic mutations. Before renaming, moving, or
deleting an apparently ordinary file, check all CLI-reported path roles,
including Stage and Action inputs. Update include spelling only when the
language-service or planner can prove the edit; otherwise complete the
filesystem/manifest operation and provide a focused follow-up action.

Exit gate: exact and conditional rules remain correct, broad globs remain
uncluttered, dirty manifests are protected, undo works for supported
transactions, and cross-boundary/generated/dependency mutations are rejected.

### Milestone 5 — Focused composition authoring

Deliver only the high-value direct-package loop first:

- distinguish direct and transitive packages;
- Add Package from discoverable providers;
- Change Requirement and Remove Direct Package;
- Restore/Lock as lifecycle actions;
- package/source navigation; and
- Explain Why Included.

Do not block the source-authoring release on a package marketplace, generic
capability editor, tool/action designer, or package-provided templates.

Exit gate: direct edits are minimal, transitive nodes cannot be mutated as
direct requirements, provider/version errors are found before application, and
every displayed composition item has provenance.

### Milestone 6 — Release hardening

Deliver:

- keyboard and screen-reader review;
- light, dark, and high-contrast review;
- Windows, Linux, macOS, remote-workspace, and multi-root coverage;
- large-workspace and watcher-storm tests;
- command/setting migration notes;
- updated README, walkthrough, screenshots, and changelog; and
- one normal VSIX dogfood cycle.

Exit gate: no critical data-loss, targeting, accessibility, or stale-state
issue remains; the old grouped tree is removed rather than maintained as a
permanent alternative.

## Verification strategy

### Contract and CLI tests

- snapshot determinism and version compatibility;
- glob, exact, Remove, Exclude, Update, and conditional membership;
- ordinary-file versus build-candidate classification;
- Build, Stage, Generate, and Action path-role discovery;
- plan preconditions and stale-document rejection;
- cross-product and cross-boundary operations;
- generated/external/dependency ownership; and
- package requirement planning and provenance.

### Extension unit tests

- tree projection and stable IDs;
- Active Product and Launch Product rules;
- Build Context versus Launch Selection persistence;
- operation serialization, cancellation, and invalidation;
- creation defaults and preview rules; and
- degraded behavior when the CLI or graph is unavailable.

### Extension-host tests

- open, locate, reveal, and refresh;
- create `.env`, Markdown, JSON, script, unknown-extension, and folder entries
  without a manifest edit;
- create single and paired items;
- include/exclude, rename/move, recoverable delete, and undo;
- dirty-manifest and stale-plan conflicts;
- F5, Ctrl+F5, product-row Run/Debug, and active-file Build targeting;
- native Testing, Problems, tasks, C++ configuration, and debug adapters; and
- keyboard command paths and accessible names.

### Canonical scenarios

- **Hello.Native:** create class, build, select launch, and debug;
- **Hello.Hosted:** package/stage behavior while runtime registration remains
  application-owned;
- **Hello.Reflection:** authored inputs, generated outputs, host tools, and
  read-only boundaries;
- **Interface/Plugin libraries:** valid item kinds and no invalid Run/Test UI;
- **multi-root:** independent workspaces and Launch Products; and
- **broken graph:** physical work continues while semantic actions explain the
  failure.

## Performance budgets

Measure on a documented local reference workspace and separately track remote
workspaces:

- show workspace/product roots without waiting for every graph;
- never perform a full recursive product walk during root refresh;
- first local folder expansion: 500 ms p95 or better;
- cached folder expansion: 100 ms p95 or better;
- watcher reconciliation visible within 500 ms after burst debounce;
- no duplicate graph request for the same product/context/input generation;
- cancellation or context changes prevent stale results from replacing current
  state; and
- all long operations remain cancellable and leave editor interaction
  responsive.

Adjust numeric budgets only from measured evidence and document the reason.

## Explicit non-goals for this overhaul

- A general replacement for VS Code Explorer outside workspace and product
  boundaries.
- A new C++ parser, language server, debugger, test runner, source-control
  client, or search engine.
- Runtime module registration or dependency-injection authoring.
- Fixed `src`, `include`, `Public`, `Private`, or other directory semantics.
- Test, Benchmark, Tool, Application, or Module product kinds.
- A dashboard webview or always-expanded Composition Graph.
- A public package marketplace.
- Automatic general C++ refactoring during file moves.
- Package-provided executable templates before a trust contract exists.
- Permanent compatibility with the superseded manifest grammar or old grouped
  tree.

## Definition of done

The overhaul is complete when a developer can open an NGIN workspace and:

1. understand its Executable and Library products and active Build Context;
2. navigate ordinary files and semantically selected inputs, identify plausible
   inputs not in the active build, and explicitly reveal ignored files;
3. create a source, header, class, and supported C++ module in co-located,
   split, public/private, header-only, and custom layouts without redundant
   manifest rules;
4. safely include, exclude, rename, move, delete, duplicate, and import owned
   files;
5. add or explain a direct package without confusing it with transitive state;
6. choose a Launch Product separately from the active file owner;
7. build, run, debug, test, analyze, and format through native VS Code
   surfaces;
8. understand generated, external, missing, conditional, and dependency-owned
   files; and
9. recover from cancellation, stale state, invalid context, or partial backend
   failure without lost text or silent manifest damage.

## What changed from the previous plan

### Kept

- the product-scoped physical tree;
- Workspace and Product terminology;
- semantic overlays, ordinary file visibility, Show Ignored Files, and
  generated/external boundaries;
- native VS Code surfaces for Testing, Problems, debug, tasks, Output, and
  Settings;
- minimal manifest edits, transactional behavior, and quiet success; and
- the CLI/Composition Graph as authority.

### Changed

- made the editor/authoring contract Milestone 0 instead of an architectural
  detail that could be deferred;
- inserted a no-visible-change architecture refactor before major features;
- separated Active Product, Launch Product, Build Context, and Run selection;
- made file membership explicitly Build-Context-dependent;
- made authoring layout-neutral and separated desired placement from semantic
  validation;
- separated ordinary physical files from plausible build inputs and made
  generic files visible by default;
- separated generic New File/New Folder from semantic C++ creation;
- made the necessary CLI/editor-planning change explicit while retaining the
  current manifest schema;
- replaced eager product enumeration with folder-lazy `workspace.fs` access;
- reduced the initial creation catalog to the few workflows worth making
  excellent; and
- changed lifecycle milestones from broad feature bundles to dependency-ordered
  gates.

### Deferred or removed

- full package/capability/tool/generator authoring from the critical path;
- a package marketplace and package-provided templates;
- immediate drag-and-drop, batch mutation, and broad automatic include
  rewriting;
- Product Settings or Composition dashboard webviews; and
- promises that the extension can safely implement before the semantic planner
  exists.

The result is smaller in visible scope for the first release, but materially
more likely to remain correct as the schema, contexts, packages, and workspace
model grow.
