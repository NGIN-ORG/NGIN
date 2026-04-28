# NGIN Package Repository And Restore Plan

## Summary

NGIN packages should be consumable from local source checkouts, installed package
stores, and remote package repositories without changing the way projects declare
dependencies. A project should continue to reference reusable packages by
identity:

```xml
<PackageRef Name="NGIN.Core" VersionRange=">=0.1.0 &lt;0.2.0" />
```

The resolver should decide where the package comes from according to local
workspace overrides, installed package stores, configured package sources, and
remote package repositories.

This plan extends the direction in
`docs/specs/009-package-distribution-and-installation.md`. The important split
is:

- `.nginpkg` is the authored package manifest.
- `.nginpack` is the distributable package archive.
- installed packages live in a local package store.
- remote package repositories publish immutable package archives and metadata.

The long-term user experience should feel similar to NuGet-style package
restore, but adapted to native C++ packages, CMake-backed builds, staged runtime
layouts, and source-backed local development.

## Goals

- Let NGIN projects consume packages from remote feeds.
- Keep `PackageRef` independent from delivery mode.
- Preserve local source development through workspace package overrides.
- Support decentralized package repositories, including official, private, and
  community feeds.
- Make package resolution deterministic and reviewable through a lock file.
- Support offline and cached builds after restore.
- Keep package archive, package manifest, and launch manifest roles distinct.
- Leave room for signatures and trust policy without blocking the first
  implementation.

## Non-Goals For The First Remote Milestone

- Do not build a hosted package service before a static feed format works.
- Do not require users to publish packages to an official repository.
- Do not require every package to be prebuilt.
- Do not make `NGIN.Core` mandatory for package consumption.
- Do not add remote fetch behavior directly to normal `ngin build` before
  restore semantics and lock-file behavior are defined.
- Do not overload `.nginpkg` to also mean a package archive.

## Current Baseline

The active repository already has useful pieces:

- package manifests use `.nginpkg`
- package archives are planned as `.nginpack`
- package references support version ranges
- workspace manifests support `PackageSources`
- workspace manifests support `PackageProviders`
- resolver code already separates package identity from provider root

The current limitations are:

- package discovery scans local folders for `.nginpkg`
- the package catalog is effectively keyed by package name only
- multiple available versions of the same package are not modeled yet
- remote package feeds do not exist
- installed package stores do not participate in resolution yet
- no restore command downloads or installs missing packages
- no lock file pins exact versions and source provenance
- source-backed packages currently rely on package-level build metadata instead
  of a normal `.nginproj` build contract

## Project And Package Contract Direction

The strongest long-term simplification is to assume that every source-backed
buildable unit has a `.nginproj`, including reusable libraries that are also
published as packages.

Under that model:

- `.nginproj` owns how source is built.
- `.nginpkg` owns how reusable package identity and published contributions are
  consumed.
- package sources, package stores, lock files, and repositories own where the
  package comes from.

This removes the current pressure for `.nginpkg` to carry local build-backend
knowledge such as `Backend="CMake"`, `Mode="AddSubdirectory"`, or package-local
CMake options. Those details belong to source projects, workspace overrides, or
provider metadata, not the reusable package contract.

Example source-backed package layout:

```text
Packages/NGIN.Core/
  NGIN.Core.nginproj
  NGIN.Core.nginpkg
  src/
  include/
```

The project file would describe build ownership:

```xml
<Project SchemaVersion="2"
         Name="NGIN.Core"
         Type="Library"
         DefaultConfiguration="Runtime">
  <Sources>
    <Public>
      <Root Path="include" />
    </Public>
    <Private>
      <Root Path="src" />
    </Private>
  </Sources>
  <Output Kind="Library"
          Name="NGIN.Core"
          Target="NGIN::Core" />
  <Build Backend="CMake"
         Mode="Generated"
         Language="CXX"
         LanguageStandard="23" />
  <References>
    <Package Name="NGIN.Base" Version=">=0.1.0 &lt;0.2.0" />
    <Package Name="NGIN.Log" Version=">=0.1.0 &lt;0.2.0" />
  </References>
</Project>
```

The package file would describe reusable consumption:

```xml
<Package SchemaVersion="3"
         Name="NGIN.Core"
         Version="0.1.0"
         CompatiblePlatformRange=">=0.1.0-alpha.1 &lt;0.2.0">
  <Exports>
    <Library Name="NGIN.Core" Target="NGIN::Core" Linkage="Static" />
  </Exports>
  <Dependencies>
    <PackageRef Name="NGIN.Base" VersionRange=">=0.1.0 &lt;0.2.0" />
    <PackageRef Name="NGIN.Log" VersionRange=">=0.1.0 &lt;0.2.0" />
  </Dependencies>
</Package>
```

This means most package authors use both files:

- `.nginproj` for source builds, tests, examples, and local development
- `.nginpkg` for package identity, dependency identity, exported artifacts,
  runtime contributions, and distribution metadata

Packages that do not own source may still only need `.nginpkg`:

- prebuilt binary packages
- content-only packages
- external system package adapters such as `SDL2`
- tool packages
- plugin bundles

## Proposed `.nginpkg` V3 Shape

A future package contract should be smaller than the current V2 contract.

Keep in `.nginpkg`:

- package identity: `Name`, `Version`
- platform compatibility
- package dependencies
- exported artifacts
- runtime modules
- runtime plugins
- content contributions
- bootstrap hooks, if they remain part of package consumption
- optional metadata needed for packaging and repository indexing

Move out of `.nginpkg`:

- source roots
- build backend
- add-subdirectory behavior
- generated build settings
- package-local CMake options
- local source checkout location
- remote acquisition details

Those moved concerns should live in:

- `.nginproj` for source build behavior
- workspace or user package source configuration for acquisition
- local provider or override metadata for active development
- `.nginpack` metadata for packed artifact layout
- lock files for exact resolved provenance

The current V2 `Artifacts` section can either remain as-is or be renamed to
`Exports` in a V3 contract. `Exports` is clearer because the package is
declaring what consumers receive, not necessarily how those artifacts were
produced.

## Source Package Build Flow

When a package reference resolves to source, the resolver should bind the package
manifest to a project:

```text
PackageRef -> Package candidate -> source root -> package project -> build output
```

Recommended source binding rules:

1. If provider metadata explicitly names a project file, use it.
2. Otherwise look for `<PackageName>.nginproj` next to `<PackageName>.nginpkg`.
3. Otherwise look for exactly one `.nginproj` in the package root.
4. If no project exists, the package can only be consumed as prebuilt,
   content-only, or metadata-only.

This keeps the common case simple while still allowing advanced source layouts.

For active local development, a workspace override could eventually become:

```xml
<PackageProviders>
  <PackageProvider
      Name="NGIN.Core"
      Root="Packages/NGIN.Core"
      Project="NGIN.Core.nginproj" />
</PackageProviders>
```

The `Project` attribute should be optional when the default discovery rule is
unambiguous.

## Why This Is Easier And More Powerful

This split makes package authoring easier:

- source packages look like normal projects
- project build behavior is not duplicated in package manifests
- application projects and library projects share one build model
- package manifests become smaller and easier to review
- package archives can contain prebuilt outputs without pretending to be source
  projects

It also makes the resolver more powerful:

- the same package identity can resolve to source, installed, or remote
  candidates
- local development overrides can point to source projects
- published packages can be restored as prebuilt artifacts
- package repositories do not need to know local source checkout structure
- lock files can pin exact package versions independently from source layout

The tradeoff is migration cost. First-party packages such as `NGIN.Core`,
`NGIN.Base`, `NGIN.Log`, `NGIN.Reflection`, and `NGIN.ECS` would need normal
package project files before package-level build metadata can be retired.

## Repository Ownership Decision

The package repository should be a separate distribution surface from this source
repository.

This repository should own:

- the `ngin` CLI
- package resolver implementation
- package archive tooling
- package repository protocol definitions
- first-party package manifests
- examples and tests
- documentation and specs

A package repository should own:

- immutable published `.nginpack` archives
- feed indexes and package metadata
- package checksums
- optional signature metadata
- optional search metadata

The first official repository can be implemented as a separate static-feed repo
or object-store bucket. It does not need a custom server at first. A static feed
keeps the protocol easy to test, mirror, self-host, and use privately.

## Terminology

Use distinct names for distinct concepts:

- Package manifest: authored `.nginpkg`
- Package archive: distributable `.nginpack`
- Package store: local extracted package cache
- Package source: a location that can provide package candidates
- Package repository: remote feed that publishes package archives
- Package provider: current local source-root override

The current `PackageProvider` name means local source override. Remote feeds
should not use that name. They should be modeled as typed package sources or
package repositories.

## Desired Resolution Order

Package resolution should be deterministic and explainable.

Recommended order:

1. workspace local provider override for a package name
2. workspace package source folders
3. workspace-local installed package store
4. user-local installed package store
5. machine-wide installed package store
6. configured remote package repositories, through restore

Normal build and validate commands should prefer already-restored packages.
Network access should happen through explicit restore or install commands first.

## Workspace And User Configuration

The current workspace manifest supports local package sources:

```xml
<PackageSources>
  <PackageSource Path="Packages" />
</PackageSources>
```

Remote feeds can be added as typed package sources:

```xml
<PackageSources>
  <PackageSource Path="Packages" />
  <PackageSource
      Name="ngin-official"
      Type="HttpFeed"
      Url="https://packages.ngin.dev/v1/index.json" />
</PackageSources>
```

Local source overrides should remain explicit:

```xml
<PackageProviders>
  <PackageProvider Name="NGIN.Base" Root="Dependencies/NGIN/NGIN.Base" />
</PackageProviders>
```

Later, `PackageProviders` may be renamed to `PackageOverrides`, but that should
be a separate compatibility decision.

User-level package source configuration should be supported later so every
workspace does not need to repeat common feeds:

```text
~/.ngin/config.xml
```

or:

```text
~/.ngin/sources.xml
```

The exact file format can be decided when the config system is introduced.

## Lock File

Remote restore needs a lock file to make builds repeatable.

Suggested file:

```text
ngin.lock
```

The lock file should contain:

- project or workspace identity
- selected package names
- exact versions
- dependency graph
- source name or repository URL
- archive hash
- extracted store path or cache key
- target OS and architecture when relevant
- resolver version

The lock file should be generated and updated by restore. Build, validate, and
graph can consume it when present.

An example logical shape:

```xml
<LockFile SchemaVersion="1">
  <Packages>
    <Package
        Name="NGIN.Core"
        Version="0.1.0"
        Source="ngin-official"
        ArchiveSha256="..."
        StoreKey="NGIN.Core/0.1.0" />
  </Packages>
  <Dependencies>
    <Package Name="NGIN.Core">
      <PackageRef Name="NGIN.Base" Version="0.1.0" />
      <PackageRef Name="NGIN.Log" Version="0.1.0" />
    </Package>
  </Dependencies>
</LockFile>
```

## Package Store Layout

The installed package store should follow the draft distribution spec:

```text
<store>/
  packages/
    NGIN.Core/
      0.1.0/
        manifest/
          package.nginpkg
        artifacts/
        content/
        metadata/
```

Recommended store locations:

- workspace store: `.ngin/packages/`
- user store: `~/.ngin/packages/`
- machine store: platform-specific later

The first implementation can support only workspace and user stores.

## Remote Feed Shape

The first feed should be static HTTP metadata plus package archives.

Suggested layout:

```text
/v1/index.json
/v1/packages/NGIN.Core/index.json
/v1/packages/NGIN.Core/0.1.0/NGIN.Core.0.1.0.nginpack
```

Feed root metadata should include:

- feed name
- protocol version
- package index location template
- archive URL template
- optional search endpoint
- optional signing policy metadata

Package index metadata should include:

- package name
- available versions
- dependency metadata summary
- compatible OS and architecture metadata when available
- archive URL
- archive hash
- publish timestamp
- deprecation or unlisted marker

The first implementation can read only the fields needed for restore and leave
search metadata for a later phase.

## CLI User Experience

Initial commands:

```bash
ngin package pack --package Packages/NGIN.Core/NGIN.Core.nginpkg --output artifacts/
ngin package inspect artifacts/NGIN.Core.0.1.0.nginpack
ngin package install artifacts/NGIN.Core.0.1.0.nginpack
ngin package restore --project Examples/App.Basic/App.Basic.nginproj --configuration Runtime
```

Later commands:

```bash
ngin package source list
ngin package source add ngin-official https://packages.ngin.dev/v1/index.json
ngin package search NGIN.Core
ngin package publish artifacts/NGIN.Core.0.1.0.nginpack --source ngin-official
```

`ngin build` should eventually offer a clear diagnostic when packages are
missing:

```text
package 'NGIN.Core' could not be resolved.
Run 'ngin package restore --project ... --configuration ...'.
```

Automatic restore during build can be considered later, but explicit restore
should come first.

## Phase 0: Contract Alignment

Purpose:

- make the intended package distribution direction explicit before code changes

Work:

- review `docs/specs/003-package-manifest-and-runtime-contributions.md`
- review `docs/specs/009-package-distribution-and-installation.md`
- review `docs/specs/011-workspace-manifest.md`
- decide whether remote feeds are represented as typed `PackageSource` entries
  or as a new `PackageRepositories` section
- decide whether lock files are workspace-level, project-level, or both

Deliverables:

- updated specs only if contract changes are agreed
- resolver terminology decision
- package source precedence decision

Exit criteria:

- no ambiguity between package manifest, package archive, package store, package
  source, package repository, and local provider override

## Phase 1: Local Archive And Store

Purpose:

- support package distribution without remote networking

Work:

- define the `.nginpack` physical container format
- implement archive metadata files
- implement checksums for every archived file
- implement `ngin package pack`
- implement `ngin package inspect`
- implement `ngin package verify`
- implement `ngin package install`
- extract packages into a workspace or user package store
- load installed package manifests from the store

Deliverables:

- packages can be packed from local manifests
- packages can be inspected without installing
- packages can be installed into a local store
- resolver can discover installed packages

Exit criteria:

- a project can resolve a package from the installed store without a workspace
  `PackageSource Path` pointing at source manifests

## Phase 2: Multi-Version Resolver

Purpose:

- make version ranges meaningful for local, installed, and later remote packages

Work:

- replace `Name -> PackageCatalogEntry` with `Name -> Version -> Candidate`
- model package candidate source type:
  - source manifest
  - provider override
  - installed package
  - remote package metadata
- implement deterministic version selection
- enforce one selected version per package identity in a composition
- improve diagnostics for version conflicts
- update `ngin graph` to show selected package versions and provenance
- update `ngin package list` and `ngin package show` for multi-version output

Deliverables:

- multiple installed versions of one package can coexist
- resolver chooses one compatible version
- conflicts fail with actionable diagnostics

Exit criteria:

- resolving `>=0.1.0 <0.2.0` chooses the expected installed version
- conflicting transitive ranges fail instead of silently picking an invalid
  version

## Phase 3: Restore And Lock File

Purpose:

- make package resolution repeatable before adding remote feeds

Work:

- define `ngin.lock`
- implement lock-file writing during restore
- implement lock-file reading during validate, graph, configure, build, and run
- add lock refresh behavior
- add locked-mode behavior
- define stale-lock diagnostics
- include package source provenance and hashes in the lock

Deliverables:

- `ngin package restore` resolves the full package graph
- restore installs missing local archive packages into the store when requested
- lock file pins exact package versions
- build and graph can explain whether they are using locked or unlocked
  resolution

Exit criteria:

- two machines with the same lock file resolve the same package versions from
  the same available stores

## Phase 4: Static Remote Feed Restore

Purpose:

- fetch packages from decentralized remote repositories

Work:

- define feed root metadata
- define package version index metadata
- support typed HTTP package sources
- implement feed metadata download
- implement archive download
- verify archive hash before install
- cache downloaded archives
- install verified packages into the local store
- support offline mode when packages are already restored
- add clear diagnostics for network failures, missing versions, and hash
  mismatches

Deliverables:

- workspace can reference an HTTP feed
- restore can download `.nginpack` archives from that feed
- restore verifies and installs packages into the store
- build can run offline after restore

Exit criteria:

- a clean checkout can restore and build a project using only a configured
  static feed and no local `Packages/` source folder

## Phase 5: Publishing And Official Feed

Purpose:

- create the first official NGIN package distribution path

Work:

- create a separate official package feed repository or storage location
- define feed publishing layout
- implement `ngin package publish` for static-feed output or upload
- generate package indexes
- support unlisted/deprecated package metadata
- publish first-party NGIN packages as `.nginpack`
- document feed mirroring and private feed setup

Deliverables:

- official feed can host first-party NGIN packages
- package authors can publish packages through a documented flow
- users can configure the official feed without cloning this repository

Exit criteria:

- a sample project can restore `NGIN.Core` from the official feed on a clean
  machine

## Phase 6: Trust, Authentication, And Search

Purpose:

- harden package consumption and improve repository UX

Work:

- define signature metadata policy
- verify package signatures when required by policy
- add repository credentials support
- support private authenticated feeds
- add package search metadata
- implement `ngin package search`
- add source priority and source mapping policy
- add repository trust prompts or explicit trust configuration

Deliverables:

- private feeds can be used safely
- organizations can restrict packages to known sources
- users can search configured feeds
- packages can be verified beyond checksum integrity

Exit criteria:

- restore can enforce source and trust policy instead of only downloading by
  name and version

## Implementation Areas

Likely code areas:

- `Tools/NGIN.CLI/src/Authoring.cpp`
- `Tools/NGIN.CLI/src/Resolution.cpp`
- `Tools/NGIN.CLI/src/Commands.cpp`
- `Tools/NGIN.CLI/src/Build.cpp`
- `Tools/NGIN.CLI/src/Model.hpp`
- `Tools/NGIN.CLI/tests/AllTests.cpp`

Likely docs:

- `docs/specs/003-package-manifest-and-runtime-contributions.md`
- `docs/specs/006-cli-contract.md`
- `docs/specs/009-package-distribution-and-installation.md`
- `docs/specs/011-workspace-manifest.md`
- `Tools/README.md`
- `Examples/README.md`

## Testing Strategy

Phase-specific tests should be added instead of relying only on end-to-end
workspace builds.

Recommended coverage:

- archive pack and inspect tests
- checksum verification tests
- package store layout tests
- multi-version resolution tests
- version conflict diagnostics
- lock-file round-trip tests
- restore-from-local-archive tests
- restore-from-static-feed tests using a local test server or file-backed feed
- offline restore/build behavior
- `App.Basic` smoke validation for installed package consumption

## Open Decisions

- Should remote feeds be represented as typed `PackageSource` entries or as a
  separate `PackageRepositories` section?
- Should the first archive container be zip, tar, or a custom simple container?
- Should `ngin.lock` live at workspace root, project root, or both?
- Should restore be explicit only, or should build offer an opt-in automatic
  restore flag?
- How should source priority be handled when the same package version exists in
  multiple sources?
- What is the first supported authentication method for private feeds?
- What signing format should be used once signatures become enforced?

## Recommended First Slice

The first useful slice should avoid remote networking.

Implement:

1. `.nginpack` archive creation
2. archive inspection and checksum verification
3. local package store installation
4. resolver support for installed packages
5. multi-version package catalog groundwork

That creates the foundation remote restore needs, while keeping the first
implementation testable and independent of feed hosting decisions.
