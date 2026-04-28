# NGIN Simplification Critique and V2 Implementation Plan

Status: Historical proposal, adopted in V2
Owner: NGIN umbrella workspace (`NGIN`)
Last updated: 2026-03-21

## Purpose

This document critiques the V1 authoring model and records the rationale for the V2 rewrite.

It intentionally discusses removed concepts such as `variant`, `SourceBinding`, and `.ngintarget`.

The original goal was:

Make NGIN feel simple by default and powerful when needed, closer to the experience of modern `.NET` application development.

This proposal assumes breaking changes are acceptable. It does not attempt to preserve the current contract surface.

## Executive Summary

NGIN currently has a strong internal architecture direction, but the authored model is still too concept-dense for ordinary application development.

The main issue is not lack of capability. The main issue is that too many concepts are first-class too early:

- workspace
- project
- package
- project reference
- package reference
- variant
- runtime modules
- plugins
- staged target manifest
- source binding
- backend build mode

That makes the system expressive, but it does not yet feel simple.

If the target is "C++ with a `.NET`-like app model", then NGIN should optimize for:

- one obvious default way to author an app
- few required concepts
- strong conventions
- optional escalation into advanced composition
- clear separation between local development concerns and reusable distribution concerns

## Current Critique

### 1. Variants are too broad and create conceptual confusion

Current variants mix several different concerns into one construct:

- build configuration
- runtime environment
- host profile
- platform
- package overlays
- module toggles
- plugin toggles
- launch selection
- sometimes effectively different apps

This is the largest source of confusion in the authoring model.

The current contract says variants are for build/run variants of the same project, but the examples and naming patterns invite users to model `Game`, `Editor`, and `DedicatedServer` as variants even when they are different applications.

Result:

- users are unsure whether they are modeling "one app with modes" or "multiple apps"
- project boundaries become blurry
- manifests become overloaded

### 2. The normal path still requires too much system knowledge

A basic app author has to understand:

- when to create a project
- when to create a package
- when to use a project ref
- when to use a package ref
- when runtime modules belong in the project
- when modules belong in a package
- how plugin enablement interacts with package defaults
- how variant-level overrides affect the resolved target

That is too much for the default path.

A simple system should let a user succeed before they understand the full graph model.

### 3. Package manifests mix reusable identity with local source/build integration

The current package model combines two separate responsibilities:

- package identity and reusable runtime/build metadata
- workspace-local source acquisition and backend integration

`SourceBinding`, `Build`, and artifact exposure are useful, but they are not the same concern as package identity.

This coupling creates several problems:

- reusable packages are described in terms of local workspace implementation details
- local source-backed development and external consumption are forced through one abstraction
- package manifests become more complex than necessary

### 4. Workspace is too central for a project-first model

The current workspace file is clean, but it is still treated as an authority for package visibility and project discovery.

For a simple authoring experience, a single project should be buildable without requiring a separate workspace container.

The workspace should be a solution-level convenience, not a requirement for ordinary development.

### 5. Runtime composition is too manifest-heavy for the default case

The current project and package manifests carry a lot of runtime composition detail:

- modules
- startup stages
- host compatibility
- services provided
- services required
- plugin-required module references

This is powerful, but it front-loads complexity into XML.

If NGIN wants to feel like `.NET`, the manifest should stay relatively thin and most application-specific composition should happen in code, or through strong conventions with optional explicit override.

### 6. Terminology is drifting

There is visible conceptual drift across the current surface:

- project
- variant
- target
- staged target

Each term has some logic behind it, but the combined result is extra cognitive load.

If a user cannot tell whether they are selecting a variant, building a target, or producing a target manifest for a project, the model is not yet sharp enough.

### 7. The authored model is optimized more for explicitness than for ergonomics

Many things are explicit that could be inferred safely in the common case:

- source discovery
- launch executable
- default host profile
- default working directory
- common platform naming
- config file inclusion

Explicitness is useful, but a default-first platform should aggressively infer the common case and reserve explicit authoring for exceptions.

## Design Goals For V2

The redesigned model should follow these rules:

1. A normal app can be authored with a single project file.
2. A workspace is optional.
3. Packages are optional for local-only development.
4. Separate executables are separate projects.
5. Configurations are narrow and unsurprising.
6. Runtime composition is code-first by default, manifest-first only when needed.
7. Reuse and distribution remain powerful, but are not part of the minimum learning path.
8. Terminology is strict and consistent across manifests, CLI, docs, and runtime.

## Proposed V2 Conceptual Model

Recommended mental model:

- workspace = optional solution container
- project = the thing you build
- reference = a dependency on another local project or reusable package
- configuration = a named set of build/runtime settings for the same project
- package = a reusable or distributable unit
- launch manifest = generated output used by tooling and runtime

### Key semantic rules

- If two deliverables have different executables or different entrypoints, they are different projects.
- If two deliverables mostly differ by `Debug`, `Release`, `Dev`, `Shipping`, diagnostics, or optional features, they are configurations of the same project.
- Packages are not required to structure local source code.
- A project can depend directly on another project in the same repo without forcing the dependency through package authoring.

## Major Breaking Changes

### Change 1: Replace `Variant` with a narrower `Configuration` model

#### What to change

- Remove `Variant` as the central authoring concept.
- Introduce `Configuration` as a narrow concept for named settings of the same project.
- Move broad identity concerns out of configuration.

Recommended configuration scope:

- build mode
- environment
- optional feature flags
- optional package additions
- optional config overlays

Do not use configuration for:

- multiple unrelated executables
- different application identities
- source root changes
- primary output changes

#### Why

This directly removes the main conceptual trap in the current model.

Users already think in terms of separate apps versus build modes. The contract should match that instinct instead of forcing an overloaded middle concept.

### Change 2: Make projects standalone and workspace-optional

#### What to change

- Allow `ngin build`, `ngin run`, `ngin validate`, and `ngin graph` to operate directly on a project file without a workspace.
- Allow package resolution from:
  - the project directory
  - user/global package locations
  - explicit resolver settings
- Keep `.ngin` only as a solution/workspace container for multi-project repos.

#### Why

This aligns with the goal of project-first authoring and reduces the ceremony of starting a new app.

### Change 3: Split local development references from reusable package references cleanly

#### What to change

- Keep `ProjectRef` for local source/build references.
- Keep `PackageRef` for reusable/distributed dependencies.
- Add a unified high-level `References` section in the authored project format, with typed entries:
  - `<Project ... />`
  - `<Package ... />`
- Internally the resolver can still distinguish them, but the authored surface should feel cohesive.

#### Why

Users should think "this project depends on these things", not "I need to choose between two structurally unrelated reference blocks before I understand the system".

### Change 4: Remove source acquisition and local backend mapping from package identity

#### What to change

- Remove `SourceBinding` from the reusable package contract.
- Move source acquisition and local source mapping to:
  - workspace resolver settings
  - local package override files
  - package source registries
- Restrict the package manifest to reusable identity and composition metadata.

Possible split:

- `.nginpkg` = package identity, dependencies, capabilities, runtime contributions, published artifacts
- workspace or resolver file = where the package comes from locally

#### Why

This is a cleaner separation of concerns:

- package contract = what the package is
- resolver contract = where it comes from

That makes local development, package publishing, and package consumption easier to reason about.

### Change 5: Make runtime composition code-first by default

#### What to change

- Reduce the default manifest responsibility for app-local runtime composition.
- Introduce or prioritize a standard application builder pattern in code.
- Keep explicit manifest-level runtime contributions only for cases that need them:
  - tooling
  - plugin discovery
  - package-published modules
  - advanced host validation

Default model:

- project manifest describes build identity and references
- application code uses NGIN hosting APIs to register runtime services/modules/features

#### Why

This matches the stated platform direction much better and makes app authoring lighter.

### Change 6: Simplify plugin and module semantics

#### What to change

- Keep plugins and modules as advanced runtime constructs, but reduce how often app authors must touch them in manifests.
- Introduce a higher-level "feature" model for common app composition.

Possible direction:

- project-level `Features` become the default authoring surface
- features map to packages, plugins, or code registrations internally
- modules remain an expert/runtime-level mechanism

#### Why

Most users want to express intent like:

- enable diagnostics
- enable reflection
- enable editor tools

They do not want to micromanage module dependency closure in the common path.

### Change 7: Tighten and unify terminology

#### What to change

Adopt one consistent set of names:

- `Project`
- `Configuration`
- `Reference`
- `Package`
- `LaunchManifest`

Avoid mixing:

- variant
- target
- selected target
- staged target

Unless a term is strictly generated/runtime-facing and clearly explained.

#### Why

Terminology drift is not cosmetic. It slows comprehension and increases modeling mistakes.

## Proposed V2 Authored Surface

This is the intended simplicity target, not a final schema.

### Minimal application project

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2"
         Name="MyGame"
         Type="Application">
  <Sources Path="src" />
  <Output Kind="Executable" Name="MyGame" />
  <References>
    <Project Path="../MyEngine/MyEngine.nginproj" />
    <Package Name="NGIN.Core" Version="2.*" />
  </References>
  <Profiles>
    <Profile Name="Debug" />
    <Profile Name="Release" />
  </Profiles>
</Project>
```

### Separate server project

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2"
         Name="MyGame.Server"
         Type="Application">
  <Sources Path="src" />
  <Output Kind="Executable" Name="MyGame.Server" />
  <References>
    <Project Path="../MyEngine/MyEngine.nginproj" />
    <Package Name="NGIN.Core" Version="2.*" />
  </References>
  <Profiles>
    <Profile Name="Debug" />
    <Profile Name="Release" />
  </Profiles>
</Project>
```

This keeps client and server as clearly separate apps.

## Implementation Plan

The plan below is intentionally detailed and assumes contracts, tooling, and examples may all change.

### Phase 1: Semantic reset and design approval

#### Change

- Approve the V2 concepts and naming.
- Decide which current concepts are preserved, renamed, narrowed, or removed.

#### Deliverables

- new architecture direction document
- new core concepts spec
- explicit deprecation list for v1 terms

#### Why first

The implementation will be expensive if semantics remain unstable.

#### Docs to update

- [docs/specs/001-core-concepts.md](/home/berggrenmille/NGIN/docs/specs/001-core-concepts.md)
- [docs/architecture/NGIN-Concepts.md](/home/berggrenmille/NGIN/docs/architecture/NGIN-Concepts.md)
- [README.md](/home/berggrenmille/NGIN/README.md)

### Phase 2: Redesign the project manifest contract

#### Change

- Replace `Variant` with `Configuration`.
- Replace separate `ProjectRefs` and `PackageRefs` blocks with a unified `References` surface.
- Rename `PrimaryOutput` to a simpler `Output` or `Outputs`.
- Keep project manifests standalone.
- Reduce required fields to the minimum needed for a buildable project.

#### Target outcome

A new `.nginproj` is valid with minimal author input.

#### Code areas likely affected

- [Tools/NGIN.CLI/src/main.cpp](/home/berggrenmille/NGIN/Tools/NGIN.CLI/src/main.cpp)
- any manifest parsers in `Packages/NGIN.Core`
- validation logic
- graph rendering logic

#### Docs to update

- replace [docs/specs/002-project-and-target-manifest.md](/home/berggrenmille/NGIN/docs/specs/002-project-and-target-manifest.md) with a v2 project spec
- update [docs/specs/004-composition-and-validation.md](/home/berggrenmille/NGIN/docs/specs/004-composition-and-validation.md)
- update [docs/specs/006-cli-contract.md](/home/berggrenmille/NGIN/docs/specs/006-cli-contract.md)

### Phase 3: Redesign the package contract and resolver boundary

#### Change

- Narrow `.nginpkg` to reusable package identity and published composition metadata.
- Remove or relocate `SourceBinding`.
- Move local source-backed acquisition into workspace/resolver metadata.
- Re-evaluate whether package `Build` belongs in the reusable manifest or in local package-provider metadata.

#### Target outcome

Package manifests describe reusable intent, not local checkout strategy.

#### Code areas likely affected

- package parsing in [Tools/NGIN.CLI/src/main.cpp](/home/berggrenmille/NGIN/Tools/NGIN.CLI/src/main.cpp)
- workspace/package discovery and sync logic
- any source binding support code

#### Docs to update

- rewrite [docs/specs/003-package-manifest-and-runtime-contributions.md](/home/berggrenmille/NGIN/docs/specs/003-package-manifest-and-runtime-contributions.md)
- update [docs/specs/009-package-distribution-and-installation.md](/home/berggrenmille/NGIN/docs/specs/009-package-distribution-and-installation.md)
- update [docs/specs/011-workspace-manifest.md](/home/berggrenmille/NGIN/docs/specs/011-workspace-manifest.md)

### Phase 4: Make workspace optional and reduce its authority

#### Change

- let projects build outside a workspace
- make workspace additive rather than mandatory
- keep workspace for:
  - multi-project discovery
  - repo-level defaults
  - local package resolver settings
  - solution-level operations

#### Target outcome

Users can create and build one project without first learning workspaces.

#### CLI changes

- support `ngin build` from a project directory by default
- support `ngin run`
- support project-local discovery without requiring `.ngin`

#### Docs to update

- rewrite [docs/specs/011-workspace-manifest.md](/home/berggrenmille/NGIN/docs/specs/011-workspace-manifest.md)
- update [docs/specs/006-cli-contract.md](/home/berggrenmille/NGIN/docs/specs/006-cli-contract.md)
- update [Tools/README.md](/home/berggrenmille/NGIN/Tools/README.md)

### Phase 5: Introduce a code-first hosted app model

#### Change

- formalize the application builder API as the default app startup pattern
- align manifests to support this model rather than replace it
- reduce the amount of app-local runtime description required in XML

#### Target outcome

The primary authoring experience becomes:

1. create a project
2. add references
3. configure the host in code
4. build and run

#### Code areas likely affected

- `Packages/NGIN.Core`
- builder APIs and samples
- app examples

#### Docs to update

- [docs/api-drafts/NGIN.Core-ApplicationModel.md](/home/berggrenmille/NGIN/docs/api-drafts/NGIN.Core-ApplicationModel.md)
- [docs/architecture/NGIN-Platform-Direction.md](/home/berggrenmille/NGIN/docs/architecture/NGIN-Platform-Direction.md)
- [README.md](/home/berggrenmille/NGIN/README.md)

### Phase 6: Simplify module/plugin authoring and add higher-level features

#### Change

- decide whether app authors should mostly work with `Features`
- keep `Modules` and `Plugins` for package/runtime internals and expert scenarios
- add conventions for common features such as diagnostics, reflection, tooling, editor support

#### Target outcome

Simple apps enable features without understanding runtime graph internals.

#### Docs to update

- rewrite runtime sections in [docs/specs/003-package-manifest-and-runtime-contributions.md](/home/berggrenmille/NGIN/docs/specs/003-package-manifest-and-runtime-contributions.md)
- update [docs/specs/004-composition-and-validation.md](/home/berggrenmille/NGIN/docs/specs/004-composition-and-validation.md)
- update [docs/specs/007-host-integration-contract.md](/home/berggrenmille/NGIN/docs/specs/007-host-integration-contract.md)

### Phase 7: Redesign the CLI around the simpler mental model

#### Change

Shift from the current explicit project/variant-centric commands to a more direct app-centric surface.

Suggested direction:

- `ngin new`
- `ngin build`
- `ngin run`
- `ngin test`
- `ngin add project`
- `ngin add package`
- `ngin add feature`
- `ngin restore`

Advanced commands can remain, but should not define the first impression.

#### Why

The CLI is the product surface. If it still speaks in terms of low-level graph plumbing, the platform will continue to feel heavier than intended.

#### Code areas likely affected

- [Tools/NGIN.CLI/src/main.cpp](/home/berggrenmille/NGIN/Tools/NGIN.CLI/src/main.cpp)
- VS Code integration under `Tools/NGIN.VSCode`

#### Docs to update

- rewrite [docs/specs/006-cli-contract.md](/home/berggrenmille/NGIN/docs/specs/006-cli-contract.md)
- update [Tools/README.md](/home/berggrenmille/NGIN/Tools/README.md)
- update extension docs and behavior notes

### Phase 8: Replace `.ngintarget` with a clearer generated launch/build artifact contract

#### Change

- re-evaluate whether `.ngintarget` should be renamed to match the new terminology, for example `.nginlaunch` or `.nginapp`
- make the generated artifact clearly runtime/tooling-facing only
- ensure authored docs do not present it as part of the normal authoring mental model

#### Why

If the user-facing model no longer revolves around "targets", the generated artifact name should not carry obsolete terminology.

#### Docs to update

- rewrite [docs/specs/005-staged-target-manifest.md](/home/berggrenmille/NGIN/docs/specs/005-staged-target-manifest.md)
- update [README.md](/home/berggrenmille/NGIN/README.md)
- update [Tools/README.md](/home/berggrenmille/NGIN/Tools/README.md)

### Phase 9: Rewrite examples around the new default path

#### Change

- rebuild examples to demonstrate the new mental model first
- ensure the smallest example needs only one project file and minimal code
- make separate executable examples separate projects, not configurations

#### Required example set

- minimal app
- app plus library project reference
- app plus package reference
- game client and headless server as separate projects
- optional workspace with multiple projects
- advanced package/runtime authoring sample

#### Docs to update

- [Examples/README.md](/home/berggrenmille/NGIN/Examples/README.md)
- example manifests under `Examples/`
- example docs under `docs/examples/`

### Phase 10: Documentation rewrite and consolidation

#### Change

- rewrite docs in the order a new user learns the system
- separate "default path" docs from "advanced runtime/package authoring"
- remove obsolete language and dual meanings

#### Documentation structure target

1. What NGIN is
2. Create a project
3. Add references
4. Configure and run
5. Multi-project workspace
6. Packaging and distribution
7. Advanced runtime composition

#### Required doc updates

- [README.md](/home/berggrenmille/NGIN/README.md)
- [Tools/README.md](/home/berggrenmille/NGIN/Tools/README.md)
- [Examples/README.md](/home/berggrenmille/NGIN/Examples/README.md)
- [docs/specs/001-core-concepts.md](/home/berggrenmille/NGIN/docs/specs/001-core-concepts.md)
- [docs/specs/002-project-and-target-manifest.md](/home/berggrenmille/NGIN/docs/specs/002-project-and-target-manifest.md)
- [docs/specs/003-package-manifest-and-runtime-contributions.md](/home/berggrenmille/NGIN/docs/specs/003-package-manifest-and-runtime-contributions.md)
- [docs/specs/004-composition-and-validation.md](/home/berggrenmille/NGIN/docs/specs/004-composition-and-validation.md)
- [docs/specs/005-staged-target-manifest.md](/home/berggrenmille/NGIN/docs/specs/005-staged-target-manifest.md)
- [docs/specs/006-cli-contract.md](/home/berggrenmille/NGIN/docs/specs/006-cli-contract.md)
- [docs/specs/007-host-integration-contract.md](/home/berggrenmille/NGIN/docs/specs/007-host-integration-contract.md)
- [docs/specs/009-package-distribution-and-installation.md](/home/berggrenmille/NGIN/docs/specs/009-package-distribution-and-installation.md)
- [docs/specs/010-workspace-and-project-model.md](/home/berggrenmille/NGIN/docs/specs/010-workspace-and-project-model.md)
- [docs/specs/011-workspace-manifest.md](/home/berggrenmille/NGIN/docs/specs/011-workspace-manifest.md)
- [docs/architecture/NGIN-Platform-Direction.md](/home/berggrenmille/NGIN/docs/architecture/NGIN-Platform-Direction.md)
- [docs/architecture/NGIN-Concepts.md](/home/berggrenmille/NGIN/docs/architecture/NGIN-Concepts.md)

## Recommended Implementation Order

This is the lowest-risk sequence while still allowing major breaks:

1. Approve naming and semantics.
2. Rewrite core concepts and architecture docs.
3. Define v2 project manifest.
4. Define v2 package/resolver split.
5. Redesign CLI commands around the new model.
6. Update parser and resolver implementation.
7. Rebuild examples.
8. Update VS Code integration.
9. Rewrite all user-facing docs.
10. Remove obsolete v1 terminology and files.

## Risks And Tradeoffs

### Risk: losing advanced power while simplifying

Mitigation:

- keep advanced runtime/package concepts available
- move them out of the minimum learning path

### Risk: making project files too implicit

Mitigation:

- use strong conventions
- keep explicit escape hatches
- document inference rules clearly

### Risk: splitting local and distributed package concerns adds implementation work

Mitigation:

- accept the work as worthwhile because it creates a cleaner long-term architecture

### Risk: code-first runtime composition may reduce static validation

Mitigation:

- preserve optional manifest-based metadata for advanced validation
- invest in host/runtime diagnostics and generated launch metadata

## Questions That Need Your Answers

These decisions affect the final shape of the plan and should be answered before implementation starts.

1. Do you want `workspace` to remain a first-class concept for repo-scale development, or should it become almost entirely optional and secondary?
2. Do you want to keep XML as the authored manifest format, or are you open to changing formats entirely for v2?
3. Should `package` remain a user-facing concept for normal app developers, or should it become mostly advanced/infrastructure-facing?
4. Do you want runtime module metadata to remain manifest-authored, or should the default move strongly toward code registration with optional generated metadata?
5. Do you want `Configuration` to cover host profile selection like `Game`, `Editor`, `Service`, or should host profile become a separate explicit axis?
6. Should NGIN prioritize local monorepo development first, package distribution first, or treat them as equal priorities?
7. Do you want a `.NET`-style CLI surface with broad default commands like `build`, `run`, and `add`, even if advanced graph commands remain available underneath?
8. Should local source-backed dependencies be modeled as projects by default, with packages reserved for distribution and published reuse?
9. Do you want the generated runtime handoff artifact to remain a visible named concept, or should it be mostly hidden from ordinary users?
10. Are you willing to split the current `NGIN.Core` responsibilities if that produces a cleaner hosted app model, or do you want to preserve the current package boundary?
11. Do you want application examples to prioritize games and editors, or should the first examples be more general-purpose applications to reinforce the platform framing?
12. Should v2 preserve any compatibility bridge for v1 manifests during transition, or do you want a hard reset with no compatibility layer?

## Recommended Answers

My current recommendation is:

- workspace should be optional
- XML can stay if you prefer, but the schema must be drastically simplified
- packages should be advanced and reusable, not mandatory for local app structure
- runtime composition should be code-first by default
- host profile should be a separate explicit axis if you still need it
- monorepo development should be optimized first
- the CLI should become much more `.NET`-like
- local source-backed dependencies should usually be projects
- generated handoff artifacts should be mostly hidden from the default path
- `NGIN.Core` may need a cleaner internal split if hosting and reusable contracts diverge
- examples should start general and then specialize into game/editor scenarios
- v2 does not need a compatibility bridge if speed and clarity matter more than migration cost

## Closing Position

The current NGIN direction is viable, but it is still shaped too much by internal architecture concerns and not enough by the authoring experience of a normal application developer.

If the goal is to be simple and powerful, the shortest path is:

- narrow configurations
- elevate projects
- demote workspace to optional
- separate local development from package distribution
- move default runtime composition toward code
- make advanced graph concepts opt-in

That would make NGIN substantially easier to understand without giving up the deeper composition model that makes it interesting.
