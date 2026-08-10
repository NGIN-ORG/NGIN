# NGIN VS Code extension implementation plan

## Purpose

Build a first-class VS Code experience for the current NGIN XML authoring model
without recreating NGIN's resolver in TypeScript. The extension should make the
common path obvious to a beginner and expose the complete Composition Graph to
an advanced user.

This replaces the deliberately minimal extension. It is not a restoration of
the pre-migration implementation: useful concepts return on top of the current
Project, Package, Workspace, selection, and graph contracts.

## Product principles

1. **The CLI owns meaning.** Structural metadata comes from `ManifestSpec` and
   semantic state comes from the CLI's resolved Composition Graph and typed
   plans. The extension never implements dependency resolution, Refinement
   precedence, capability binding, package activation, or backend mapping.
2. **The editor owns interaction.** Discovery, the active selection, views,
   status, process presentation, diagnostics, cancellation, and document edits
   are legitimate extension responsibilities.
3. **XML remains the source.** Every authoring action produces ordinary XML,
   participates in undo/redo, preserves comments and unknown extension nodes,
   and remains understandable without VS Code.
4. **One selected context.** Workspace, project, Configuration, Target,
   Toolchain, and Option overrides form one visible `NginContext` used by every
   command and provider. Presets remain command-specific one-shot invocations,
   matching the CLI contract; a build Preset cannot silently affect run or
   publish.
5. **Resolved facts do not get guessed.** A failed or stale graph is shown as
   unavailable. Physical files may still be browsed, but semantic membership,
   packages, actions, launch, and tooling are never inferred from XML.
6. **Progressive disclosure.** The activity view and status bar cover the
   normal loop. Graph identities, provenance, policies, and exact invocations
   are available when requested rather than always occupying the interface.
7. **Trust and safety are visible.** Commands that execute a process require a
   trusted workspace. File mutations are bounded to the selected project and
   use VS Code workspace edits or trash operations.

## User experience

### Activity-bar container

The NGIN container contains two native trees.

`Solution` presents:

- the discovered workspace or standalone context;
- projects, with the active project clearly marked;
- Configuration, Target, Toolchain, and Preset choices;
- quick lifecycle actions; and
- actionable unavailable/error states.

`Active Project` presents graph-derived groups:

- product and effective selection;
- project files, including selected, unselected, generated, external, and
  missing states;
- packages and active Exports;
- Options and Capability bindings;
- Actions and Plugins;
- Launch, Testing, Stage contributions, and Publish definitions; and
- Composition identity and provenance.

Tree nodes open their source location where graph provenance is available.
Semantic nodes can run `NGIN: Explain Selection` without copying an identity by
hand.

### Status bar

Compact items show the active project, Configuration, Target/Toolchain, and the
primary Build and Run/Debug actions. Tooltips contain the complete effective
selection and output directory. Errors change the project item to a warning
instead of silently retaining apparently valid stale data.

### Project dashboard

`NGIN: Open Project Dashboard` opens a webview beside the XML source. It is a
resolved overview, not an alternate serialization:

- product identity and selection;
- dependency/export and Option summaries;
- build input and deployment counts;
- launch/test/publish actions; and
- direct links to XML, graph JSON, validation, and lifecycle commands.

Edits are performed by explicit commands and VS Code `WorkspaceEdit`; the
dashboard never rewrites a document from a JavaScript object model.

### Commands and tasks

The extension exposes selection, authoring, inspection, and lifecycle commands:

- select workspace, project, Configuration, Target, and Toolchain, or run a
  declared Preset command;
- validate, restore, configure, build, rebuild, stage, run, test, and publish;
- analyze and format;
- show graph, inspect, diff, explain, output, and dashboard;
- add a package, project reference, or Action through the CLI;
- create project files and include/exclude their exact membership; and
- refresh or reveal the active context.

An `ngin` task provider supplies configure/build/stage/test/clean-style tasks
for the selected project. Task execution and commands share the same argument
builder, so a task cannot accidentally target a different context.

### Debugging

The contributed `ngin` debugger is a configuration resolver. It stages the
selected product, reads product and launch intent from the resolved graph, and
returns a native C++ debug configuration:

- `cppvsdbg` on Windows;
- `cppdbg` with GDB/LLDB elsewhere; and
- the staged executable, working directory, arguments, and environment from
  the selected Launch.

User-supplied debugger fields are retained where they do not contradict the
resolved executable context. No debugger executable is inferred before a
successful graph and stage.

### C/C++ language service

Configure creates `<output>/cmake/compile_commands.json`. The
extension registers an optional C/C++ custom configuration provider that:

- reads the compile database after configure/build;
- selects the exact or nearest translation unit for headers;
- maps compiler, include, define, forced-include, standard, and compiler-arg
  data into the C/C++ extension API;
- refreshes when the compile database or selection changes; and
- falls back to the graph's include/define/language facts when a compile entry
  is temporarily unavailable.

NGIN does not write `c_cpp_properties.json`; that would create a second state
file that can drift from the active selection.

### Manifest authoring and diagnostics

Generated editor metadata drives completion and hover documentation. Semantic
validation runs through the CLI on save and on demand. Diagnostics are parsed
into file/line/column entries and stale diagnostics are cleared per validation
run.

The extension's XML utility is deliberately lexical. It may locate elements,
attributes, indentation, and insertion ranges, but it may not decide semantics.
Edits are narrowly scoped and followed by CLI validation. Formatting delegates
to `ngin manifest format`.

Project file membership uses typed `<Source>`, `<Header>`, `<CxxModule>`, and
`<Resource>` items. Include and exclude operations add exact rules while
preserving existing globs; they do not expand or normalize the full manifest.

## Architecture

```text
extension.ts
  -> NginController                  active context and lifecycle
       -> ManifestDiscovery          workspace/project candidates
       -> NginCli                    process execution and diagnostics
       -> GraphStore                 resolved graph/cache/invalidation
       -> SelectionStore             persisted UI selection
  -> SolutionTreeProvider            authored discovery + selection
  -> ProjectTreeProvider             resolved graph + physical files
  -> DashboardProvider               resolved overview and command bridge
  -> NginTaskProvider                VS Code tasks
  -> NginDebugProvider               native debug configuration
  -> CppConfigurationProvider        compile database integration
  -> ManifestLanguageFeatures        completion, hover, validation
  -> StatusBarController             compact active context
```

### Core data boundary

`NginContext` contains only interaction inputs:

- workspace manifest path, if present;
- project manifest path;
- Configuration, Target, and Toolchain;
- explicit Option overrides; and
- deterministic output directory.

`CompositionGraph` types mirror the public canonical JSON payload. They are
read-only presentation types, not resolver types. Unknown graph properties are
ignored and missing required envelope/product/selection properties make the
payload unavailable with a visible error.

### Process model

All CLI calls flow through one service with:

- an argument-array API (never shell-string interpolation);
- workspace trust checks for mutating/executing commands;
- a dedicated NGIN output channel;
- cancellation and one active operation per project;
- exact exit-code preservation;
- separate stdout/stderr capture for JSON commands; and
- diagnostic extraction with Windows-path-safe locations.

### Caching and invalidation

Discovery is cached per VS Code workspace folder. Graphs are cached by the
complete `NginContext` key. Manifest changes invalidate discovery/graph data;
source changes invalidate only physical files and compile-command coverage.
Lifecycle completion refreshes graph, artifacts, trees, dashboard, status, and
C/C++ configuration through one event.

## Implementation sequence

### 1. Foundation

- Introduce strict shared types and pure path/selection helpers.
- Implement discovery for `.ngin` and `.nginproj`, excluding generated roots.
- Implement workspace vocabulary extraction for selection choices.
- Implement the CLI service, graph parsing, diagnostics, output paths, state,
  persistence, watchers, and refresh events.
- Add focused unit tests for pure functions and malformed data.

### 2. Daily workflow

- Add the activity container, Solution and Active Project tree providers.
- Add selection Quick Picks and status-bar items.
- Add validate/restore/configure/build/stage/run/test/publish/analyze/format and
  inspection commands.
- Add cancellation, progress, output, trust handling, and task provider.

### 3. Native development

- Add deterministic compile-database paths and parsing.
- Register the C/C++ custom configuration provider when C/C++ is installed.
- Add the `ngin` debug resolver and stage-before-debug flow.
- Watch generated artifacts and refresh consumers.

### 4. Authoring and project navigation

- Add physical file enumeration and graph membership decoration.
- Add source navigation and reveal commands.
- Add safe file/folder creation, rename, duplicate, and trash deletion.
- Add exact typed include/exclude membership edits.
- Add CLI-backed dependency/reference/Action commands.
- Add hover metadata, semantic diagnostics, and the resolved project dashboard.

### 5. Hardening and delivery

- Restore focused unit tests and extension-host integration tests.
- Test standalone projects, multi-root workspaces, paths containing spaces,
  stale/missing CLI, invalid graphs, failed builds, and untrusted workspaces.
- Update the README, changelog, command/configuration reference, and packaging
  scripts.
- Run typecheck, unit tests, build, VSIX packaging, and a VS Code extension-host
  smoke test.

## Completion criteria

- A new user can open the repository, see the projects, choose one, build it,
  run it, and understand failures without editing settings JSON.
- The selected workspace/project/Configuration/Target/Toolchain is visible and
  is identical across views, tasks, build, run, debug, and C/C++ configuration.
- The Active Project tree is derived from canonical graph JSON and exposes all
  current graph categories plus provenance/explanation.
- Configure/build produces usable IntelliSense through the compile database.
- Debug stages and launches the graph-selected executable with graph-selected
  process intent.
- The dashboard and authoring commands preserve XML source, comments, unknown
  registered extensions, and undo/redo.
- No TypeScript code resolves dependencies, evaluates Refinements, selects
  capabilities, interprets backend integration, or invents runtime behavior.
- Unit and extension-host tests cover discovery, selection, diagnostics, graph
  mapping, paths, XML edits, trees, commands, C/C++ mapping, and debug mapping.
- `npm run typecheck`, `npm test`, `npm run build`, and VSIX packaging succeed.

## Explicit non-goals

- Reimplementing the CLI parser or Composition Graph resolver.
- Supporting a build adapter other than the CLI's current CMake adapter.
- Editing package integration bindings through bespoke controls.
- Writing persistent VS Code-specific project metadata into NGIN manifests.
- Treating a Preset label as semantic graph identity.
- Silently accepting obsolete manifest grammar.

## Implementation status

Implemented on 2026-08-10.

- The extension contributes two native views, a resolved dashboard, status-bar
  state, 45 commands, an `ngin` task provider, and an `ngin` native debug
  configuration resolver.
- Workspace discovery, selection persistence, CLI execution, diagnostic
  mapping, graph parsing, process cancellation, and deterministic output paths
  are shared foundation services.
- C/C++ integration consumes `<output>/cmake/compile_commands.json` through the
  official custom-configuration-provider API and falls back to graph build
  facts without writing `c_cpp_properties.json`.
- Project Files combines the physical tree with graph membership and provides
  bounded file operations and exact typed membership edits. Product edits use
  the same lossless lexical edit layer; dependency/reference/Action authoring
  delegates to the CLI.
- `npm test` passes 13 focused unit cases plus an extension-host smoke test
  against `Hello.Native` and the repository CLI.
- `Hello.Native` stages successfully at the extension's output layout with both
  `<output>/cmake/compile_commands.json` and
  `<output>/stage/bin/Hello.Native.exe` present.
- `npm run package` produces a 19-file VSIX containing the bundled extension,
  schemas, snippets, grammar, icons, README, changelog, and license.
