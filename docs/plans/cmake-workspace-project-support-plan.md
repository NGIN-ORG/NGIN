# CMake Projects in NGIN Workspaces — Implementation Plan

## Status

Proposed, August 2026.

This plan adds first-class development support for explicitly registered CMake
projects in NGIN workspaces. It is intentionally narrower than a generic
multi-build-system or third-party provider architecture.

The design uses **Workspace Project** as the common workspace concept and
**Project System** as the implementation discriminator. NGIN and CMake remain
authoritative for their own semantics. The native `ngin` CLI owns discovery,
identity, capabilities, lifecycle dispatch, and versioned editor snapshots;
NGIN Tools for VS Code consumes that contract.

## Decision summary

Approve the following direction:

> NGIN workspaces can contain native NGIN projects and explicitly registered
> CMake projects. Each project system remains authoritative for its own
> semantics, while the NGIN CLI provides common discovery, stable identity,
> capabilities, lifecycle dispatch, and editor snapshots.

Do not describe the result as a provider-neutral workspace platform for
arbitrary build systems. Introduce another project system only when a concrete
third use case requires it.

## Problem

The root NGIN workspace discovers native `.nginproj` products and package
manifests, but the first-party libraries under `Dependencies/NGIN` are
standalone CMake projects. In particular, NGIN.Base and NGIN.Reflection own
useful configure presets, targets, tests, examples, and benchmarks that are not
directly available as development projects in the NGIN Workspace view.

Their package manifests make the libraries consumable by NGIN products, but a
package wrapper is not a development-project description. A shadow
`.nginproj` would also be incorrect: one NGIN project represents one physical
product, while one CMake project may define many targets and configurations.

The solution must work for any NGIN workspace that explicitly includes an
existing CMake project. It must not depend on repository-specific paths or
names.

## Goals

- Allow `<Discover><Projects>` to include native NGIN project manifests and
  CMake project directories.
- Show both project systems in one NGIN Workspace view.
- Keep project discovery and CMake integration authoritative in the CLI.
- Reuse CMake presets without implementing a second preset evaluator.
- Use the CMake File API for targets, sources, artifacts, and provenance.
- Use CTest for test discovery and execution.
- Scope project selection and CMake context to the owning NGIN workspace.
- Require workspace trust before executing CMake from VS Code.
- Preserve current native NGIN project, package, graph, build, stage, and run
  behavior.
- Establish an explicit package-to-development-project relationship.

## Non-goals

- Do not require `.nginproj` files for CMake projects.
- Do not synthesize an NGIN Composition Graph for CMake targets.
- Do not recursively discover every nested `CMakeLists.txt`.
- Do not initially support loose standalone CMake folders without an NGIN
  workspace.
- Do not require the CMake Tools extension.
- Do not parse or rewrite arbitrary CMake source code.
- Do not implement a public third-party project-system ABI.
- Do not infer Benchmark, Run, or Debug behavior from target names or artifact
  type alone.
- Do not change package resolution or Composition identity through development
  metadata.

## Terminology

Use the following terms consistently in the CLI, protocol, extension, and
documentation:

| Term | Meaning |
| --- | --- |
| Workspace Project | A developable project included in an NGIN workspace |
| Project System | The authoritative implementation model: `Ngin` or `CMake` |
| Owning Project | Project derived from the active editor file |
| Active Project | Explicit user-selected fallback, scoped to one workspace |
| Launch Target | Explicit executable and launch state used by Run or Debug |
| CMake Context | Configure preset plus configuration for a multi-config generator |

Selecting or expanding a tree row is navigation only. It must not change the
Owning Project, Active Project, Launch Target, or CMake Context.

The user-facing command remains **Select Active Project**. The persisted model
may name its field `selectedProjectId` to make the state transition explicit.

Avoid the term **Project Provider** because NGIN already uses **Package
Provider** for a different contract. Internal implementations may use a
`ProjectSystemDriver` interface, but it is not a public plugin API.

## Authored workspace contract

Retain the current `<Discover><Projects>` structure and generalize the accepted
project candidate from only `.nginproj` files to either project manifests or
project directories:

```xml
<Workspace Name="NGIN">
  <Discover>
    <Projects Include="Tools/NGIN.CLI/NGIN.CLI.nginproj" />
    <Projects Include="Examples/**/*.nginproj" />

    <Projects
        Include="Dependencies/NGIN/NGIN.Base"
        System="CMake" />

    <Projects
        Include="Dependencies/NGIN/NGIN.Reflection"
        System="CMake" />

    <Packages Include="Packages/**/*.nginpkg" />
  </Discover>
</Workspace>
```

`System` is optional. Resolution infers it when there is exactly one valid
interpretation:

- a `.nginproj` file resolves as `Ngin`;
- a directory with supported CMake root metadata resolves as `CMake`;
- an ambiguous candidate requires an explicit `System`;
- an unsupported candidate produces a workspace diagnostic.

Explicit discovery patterns define the boundary. The resolver must not scan
arbitrary descendants for CMake projects.

Glob expansion, canonicalization, duplicate detection, and diagnostics must be
deterministic. Display names are not identities.

## Resolved model

The CLI workspace model should converge on a structure equivalent to:

```cpp
enum class ProjectSystem
{
    Ngin,
    CMake,
};

struct WorkspaceProject
{
    ProjectId id;
    std::string name;
    ProjectSystem projectSystem;
    std::filesystem::path root;
    ProjectCapabilities capabilities;
};
```

Stable project identity must include the canonical workspace identity, project
system, and canonical project root or manifest. Two workspaces may include the
same physical project and still maintain independent selection state.

Project-system-specific models remain tagged rather than being flattened into
one universal graph:

```text
WorkspaceProject
  NginProjectModel
    SemanticProject
    CompositionGraph

  CMakeProjectModel
    Configure state
    Codemodel
    Targets
    Sources
    Artifacts
```

## Capability model

Commands and context menus are enabled from explicit capabilities instead of
project-name, target-name, or file-layout heuristics.

Initial capabilities are:

| Capability | NGIN | CMake version 1 |
| --- | ---: | ---: |
| Inspect | Yes | Yes |
| Configure | Through NGIN lifecycle | Yes |
| Build | Yes | Yes |
| Build Target | Resolved product target | Yes |
| Test | NGIN registrations | CTest |
| Source Ownership | Composition Graph | CMake codemodel |
| Open Declaration | Authored manifest | CMake backtrace |
| Artifacts | Composition Graph | CMake codemodel |
| Stage | Yes | No |
| Composition Graph | Yes | No |
| Run/Debug | Run and launch semantics | Deferred |
| Benchmark | NGIN registration | Deferred |
| Semantic Authoring | CLI authoring plan | Deferred/opt-in |

Unsupported capabilities are absent. The UI must not offer a command and wait
for it to fail merely because the project uses another project system.

## CMake project system

### Detection

An explicitly discovered directory is a CMake candidate when it contains a
supported root `CMakeLists.txt`, `CMakePresets.json`, or
`CMakeUserPresets.json`. The final minimum CMake version and accepted root
files must be documented before implementation.

Detection reads authored files only. It must not configure or execute CMake.

### Presets and context

CMake remains authoritative for preset semantics. NGIN may read safe display
metadata, but it must delegate inheritance, conditions, macro expansion,
environment processing, toolchains, and generator selection to CMake.

The persistent context contains:

```text
Configure preset
Configuration, for multi-config generators
```

Build and test presets are choices made for individual operations. They are
not permanent dimensions of the active context.

When the project has no usable configure preset, version 1 should report an
actionable diagnostic rather than inventing or writing a preset. Managed
non-preset configurations may be considered later.

### Configure

Configure is always explicit and performs these steps:

1. verify workspace trust when invoked from VS Code;
2. validate the selected configure preset through CMake;
3. create a client-owned File API query in the selected build directory;
4. invoke CMake with the selected preset;
5. read the newest valid File API reply index;
6. update the CMake project snapshot;
7. retain the last successful codemodel if configuration fails; and
8. return structured diagnostics and stale state.

Automatic configure defaults to off.

### File API

The initial client query should request the supported versions of:

- `codemodel`;
- `cmakeFiles`;
- `toolchains`; and
- cache metadata only to the extent required for safe presentation.

The resolved CMake model should expose:

- configurations and directories;
- target IDs, names, types, and dependencies;
- source files and source groups;
- compile groups required for source ownership and editor configuration;
- artifacts;
- declaration backtraces; and
- the CMake inputs which invalidate the model.

Do not expose arbitrary cache contents or environment values in editor
snapshots.

### Build

Build dispatch uses CMake itself:

- invoke a selected build preset when supplied for the operation; or
- invoke `cmake --build` for the configured build directory and requested
  target;
- pass the configuration explicitly for multi-config generators.

Every operation is keyed by workspace ID, project ID, CMake Context, and target
ID. Target display names alone are not sufficient identifiers.

### Test

Use CTest for discovery and execution. Support:

- all tests in the configured project;
- selected tests;
- test presets chosen for an operation;
- multi-config configuration selection;
- structured results and output-on-failure; and
- VS Code Testing integration.

Do not infer tests by searching for executable names or directories.

### Run, Debug, and Benchmark

Defer these capabilities until an explicit launch contract defines executable,
arguments, working directory, environment, configuration, and runtime
dependencies.

An executable artifact is necessary but insufficient launch information.
Similarly, a target containing `benchmark` in its name has no universal
benchmark meaning.

## Workspace trust and safety

CMake configure executes arbitrary project code. Workspace trust is therefore
a phase-one requirement rather than a later editor enhancement.

In an untrusted VS Code workspace:

- display workspace and project identity;
- permit safe physical navigation;
- permit reading declared, non-secret presentation metadata;
- disable Configure, Build, Test, Run, and Debug; and
- explain that the command requires workspace trust.

The CLI does not silently configure during discovery or inspection. Commands
which execute CMake remain explicit.

Editor snapshots must exclude secrets from environment variables, cache
entries, presets, and toolchain state. Diagnostics must avoid echoing full
secret-bearing command lines.

Clean operations must distinguish:

- a build directory created and owned by NGIN;
- a build directory selected by an authored CMake preset; and
- an unknown directory.

NGIN must not recursively delete a build directory unless its ownership and
exact boundary are proven. A normal CMake clean target is preferable to
directory deletion.

## Editor protocol

Extend the versioned CLI editor protocol with project-system and capability
information. If the existing version 1 contract has shipped, use a new
protocol version for breaking shape changes. If it has not shipped, finalize
the shape before release rather than silently changing it afterward.

An illustrative workspace snapshot is:

```json
{
  "kind": "NGIN.EditorWorkspaceSnapshot",
  "version": 2,
  "workspaces": [
    {
      "id": "workspace-id",
      "name": "NGIN",
      "projects": [
        {
          "id": "hello-native-id",
          "name": "Hello.Native",
          "projectSystem": "Ngin",
          "root": "Examples/Hello.Native",
          "manifest": "Examples/Hello.Native/Hello.Native.nginproj",
          "capabilities": ["Inspect", "Build", "Stage", "Run", "Test"]
        },
        {
          "id": "ngin-base-id",
          "name": "NGIN.Base",
          "projectSystem": "CMake",
          "root": "Dependencies/NGIN/NGIN.Base",
          "capabilities": [
            "Inspect",
            "Configure",
            "Build",
            "BuildTarget",
            "Test",
            "SourceOwnership",
            "Artifacts"
          ],
          "cmake": {
            "activeConfigurePreset": "tests",
            "configuration": "Debug",
            "configured": true,
            "stale": false
          }
        }
      ]
    }
  ]
}
```

The exact protocol should use tagged project-system payloads and stable target
IDs. It should report unavailable capabilities and degraded project state
without removing safe physical navigation.

The extension must not independently parse CMake presets, File API replies, or
CTest results.

## VS Code experience

The Workspace view presents projects using familiar language:

```text
NGIN
  Projects
    Hello.Native                 NGIN · Debug
    NGIN.Base                    CMake · tests
      Targets
        NGIN.Base.Foundation
        NGIN.Base.IO
        NGINBaseTests
      Source
      Tests
    NGIN.Reflection              CMake · development
```

Each project row shows its project system and current context. The Active
Project receives the existing visible selected-project icon. Owning Project is
derived from the active file and may differ without changing the icon or
persisted selection.

CMake project actions initially include:

- Select Active Project;
- Select Configure Preset;
- Configure;
- Build;
- Build Target;
- Test;
- Open Project Root; and
- Refresh Project Model.

Target actions initially include:

- Build Target;
- Show Sources;
- Show Artifacts; and
- Open Owning Declaration when a trustworthy backtrace exists.

Run, Debug, and Benchmark are absent until the project advertises those
capabilities.

## Package development relationship

Package consumption and project development are distinct. Do not infer their
relationship from CMake target names or wrapper implementation details.

Add optional non-semantic metadata equivalent to:

```xml
<Package Name="NGIN.Base" Version="0.1.0">
  <Development Project="../../Dependencies/NGIN/NGIN.Base" />
  <!-- Existing package content -->
</Package>
```

Before implementation, record whether the relationship belongs in the package
manifest or the containing workspace. Whichever location is selected, the
contract must guarantee that it:

- does not affect package resolution, identity, fingerprints, or lockfiles;
- may be absent in installed or cached packages;
- is resolved only as a development navigation relationship;
- does not make package validation fail when the source checkout is absent;
  and
- resolves to an explicitly discovered workspace project before commands are
  enabled.

The relationship enables:

- Open Development Project;
- Show Consuming Products;
- Show Exported Targets; and
- navigation between package wrapper, source project, and consumers.

## File authoring

Version 1 CMake support is read-only with respect to build declarations.
Physical New File and New Folder remain available, but creating a file does not
claim to add it to a CMake target.

Safe initial actions are:

- create a physical file or folder;
- reveal source ownership after reconfiguration;
- open an owning CMake declaration through File API backtrace data; and
- explain that target membership is controlled by CMake.

General CMake mutation is unsafe because membership may be produced by
variables, functions, conditions, generator expressions, included files,
globs, or generated sources.

An opt-in structured authoring contract may be designed later for first-party
projects. It must return a reviewable authoring plan and advertise an explicit
capability. It must not become an implicit arbitrary-CMake rewrite feature.

## Delivery phases

### Phase 1 — Workspace identity, protocol, and trust

- Define stable workspace and project IDs.
- Add `projectSystem` and project capabilities to resolved snapshots.
- Scope Active Project state per workspace.
- Preserve Owning Project as active-file-derived state.
- Define stale and degraded project states.
- Add VS Code workspace-trust gating.
- Define build-directory ownership and safe Clean rules.
- Finalize the editor protocol versioning decision.

**Exit criteria:** two workspaces may contain projects with the same display
name without state collision; untrusted workspaces cannot execute CMake.

### Phase 2 — Generalized project discovery

- Extend workspace project entries to accept directories.
- Add `System="CMake"` with deterministic inference when omitted.
- Resolve explicit CMake project roots without recursive scanning.
- Report unsupported, missing, duplicate, and ambiguous candidates.
- Preserve existing `.nginproj` discovery behavior.
- Update the workspace reference and CLI schema.

**Exit criteria:** an authored workspace snapshot contains both native NGIN and
explicit CMake projects with stable IDs and capability placeholders.

### Phase 3 — CMake inspection

- Implement the built-in CMake project-system driver.
- Enumerate available configure presets through CMake.
- Add explicit Configure.
- Write client-owned File API queries.
- Parse configurations, directories, targets, sources, artifacts, compile
  groups, dependencies, and backtraces.
- Track CMake inputs and stale state.
- Retain the last successful model after a failed configure.
- Return structured diagnostics without secret-bearing state.

**Exit criteria:** the CLI can configure NGIN.Base or a focused CMake fixture
and return a deterministic project snapshot containing its targets and source
ownership.

### Phase 4 — CMake lifecycle

- Build through an operation-selected build preset.
- Build a target through the configured build directory.
- Support single-config and multi-config generators.
- Discover tests through CTest.
- Run all or selected tests and return structured results.
- Integrate progress, cancellation, and operation coordination.
- Implement safe model refresh after lifecycle operations.

**Exit criteria:** the CLI can configure, build a selected target, and run a
selected CTest test without the editor interpreting terminal output.

### Phase 5 — Unified VS Code workspace

- Render NGIN and CMake projects in the same Workspace view.
- Display project-system and context descriptions.
- Add capability-driven context menus.
- Preserve the visible Active Project selection indicator.
- Resolve Owning Project from active-file source ownership.
- Add lazy physical navigation for CMake projects.
- Show targets, sources, artifacts, stale state, and diagnostics.
- Connect CTest discovery and execution to VS Code Testing.

**Exit criteria:** NGIN.Base and NGIN.Reflection can be configured, inspected,
built, and tested from the extension while native example behavior remains
unchanged.

### Phase 6 — Package development relationships

- Finalize the non-semantic relationship location and schema.
- Resolve the relationship only to explicitly discovered projects.
- Add Open Development Project.
- Add consuming-product and exported-target navigation.
- Preserve package identity, resolution, restore, and lock behavior.

**Exit criteria:** the NGIN.Base package can navigate to its CMake development
project without CMake-path or target-name heuristics.

### Phase 7 — Launch and optional authoring

- Define explicit CMake launch state.
- Add Run and Debug only when the full launch contract is available.
- Investigate opt-in structured CMake source authoring.
- Evaluate loose-folder standalone CMake mode using implementation experience
  and user demand.

This phase is not required to declare first-class CMake workspace development
complete.

## Verification plan

Add focused tests instead of relying on the full repository as the primary
fixture.

### CLI tests

- existing native project discovery remains unchanged;
- explicit CMake directory discovery;
- inferred and explicit project systems;
- unsupported and ambiguous project diagnostics;
- deterministic canonical IDs and duplicate handling;
- multiple workspaces with identically named projects;
- preset enumeration delegated to CMake;
- File API parsing for single- and multi-target projects;
- single-config and multi-config behavior;
- one source owned by multiple targets;
- artifact and backtrace resolution;
- stale model retention after failed configure;
- CTest discovery and structured results;
- redaction of cache and environment secrets;
- package development metadata excluded from semantic identity.

### VS Code tests

- workspace trust command gating;
- independent Active Project state per workspace;
- Owning Project does not silently change Active Project;
- tree selection is navigation-only;
- CMake context is stored per CMake project;
- capability-driven project and target menus;
- selected-project icon changes visibly;
- target/source ownership navigation;
- CTest integration;
- unavailable Run, Debug, and Benchmark actions remain hidden;
- native NGIN product workflows remain unchanged.

### Repository smoke validation

After focused tests pass, validate one first-party project with its existing
preset contract. NGIN.Base is the preferred first smoke project because it
exercises multiple component targets and tests. NGIN.Reflection is the second
smoke project when tool or generator target behavior is affected.

Do not configure or build every dependency project by default.

## Documentation updates

When implementation begins, update these contracts in the same change as the
behavior they describe:

- workspace manifest reference;
- CLI reference and schema output;
- editor protocol reference;
- VS Code extension README and changelog;
- package manifest reference if development metadata is package-owned; and
- Composition Graph reference to state explicitly that CMake project models
  are not Composition Graphs.

## Risks and mitigations

| Risk | Mitigation |
| --- | --- |
| Project System becomes a premature plugin abstraction | Keep NGIN and CMake built in; add no public ABI |
| CMake preset behavior diverges | Delegate evaluation and execution to CMake |
| CMake execution runs untrusted code | Require explicit action and VS Code workspace trust |
| Secrets leak through snapshots | Whitelist safe fields and redact diagnostics |
| Recursive discovery includes dependencies or fixtures | Only resolve explicit workspace project patterns |
| Target names collide | Use stable project-scoped target IDs |
| Failed configure removes navigation | Retain physical tree and last successful codemodel |
| Package/project association is guessed incorrectly | Require an explicit non-semantic relationship |
| Arbitrary CMake edits corrupt authored logic | Keep build declaration authoring out of version 1 |
| Native NGIN behavior regresses | Preserve current discovery paths and add mixed-workspace tests |

## Review decisions required before implementation

1. Confirm the authored `System` spelling and allowed values.
2. Confirm whether CMake projects without presets are rejected in version 1.
3. Confirm the minimum supported CMake and File API versions.
4. Decide whether package development relationships live in package or
   workspace manifests.
5. Confirm whether the current editor protocol version 1 is already considered
   published and therefore requires a version 2 shape.
6. Define which CMake cache and toolchain fields are safe for editor snapshots.
7. Define build-directory ownership persistence and Clean behavior.
8. Confirm that loose-folder CMake, Run, Debug, Benchmark, and CMake authoring
   remain deferred.

## Completion criteria

The initial feature is complete when:

- an NGIN workspace explicitly includes both `.nginproj` and CMake projects;
- the CLI returns stable project identities, systems, and capabilities;
- NGIN Tools shows those projects in one workspace without repository-specific
  logic;
- each workspace maintains an independent Active Project;
- active-file ownership resolves an Owning Project without changing selection;
- a trusted user can configure, inspect, build, and test a CMake project;
- CMake remains authoritative for presets and codemodel semantics;
- untrusted workspaces cannot execute CMake;
- native NGIN Composition, package, build, stage, run, and authoring behavior
  remains unchanged; and
- no shadow `.nginproj`, recursive CMake scan, CMake Tools dependency, or
  arbitrary CMake rewrite is introduced.
