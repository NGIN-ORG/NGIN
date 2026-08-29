# Editor protocol

The native `ngin` CLI owns the semantic contract used by editors. Native
product snapshots and authoring plans remain version 1. The mixed-project
workspace and CMake project snapshots use version 2:

```text
ngin editor workspace --workspace NGIN.ngin
ngin editor snapshot --project App.nginproj --configuration Debug
ngin editor snapshot --project Dependencies/NGIN/NGIN.Base --configure-preset tests --configuration Debug
ngin editor plan --project App.nginproj --configuration Debug \
  --intent CreateItems --item 'Source||src/Renderer.cpp'
```

Responses are deterministic JSON envelopes. Consumers reject unsupported
versions rather than guessing at missing fields.

## Product snapshot

`NGIN.EditorProductSnapshot` version 1 contains:

- the stable product identity, manifest, and physical boundary;
- the effective Configuration, Target, and Toolchain;
- a SHA-256 precondition for the authored manifest;
- distinguishable Build, Stage, Action input, and Action output path roles;
- generated state, visibility, ownership, and provenance; and
- protocol capabilities.

The snapshot complements the Composition Graph. It is editor-facing and may
add presentation-safe path-role information, while all membership and
provenance still derive from authored semantics and the resolved graph.

## Workspace snapshot

`NGIN.EditorWorkspaceSnapshot` version 2 contains authored workspaces, their
stable identities and boundaries, mixed Workspace Projects, package
development relationships, direct consumers, authored package exports,
standalone native projects, and diagnostics. Each
project supplies `projectSystem` (`Ngin` or `CMake`) and an explicit capability
list. Editors use it for discovery and keep physical navigation available when
an individual project model is degraded.

## CMake project snapshot

`NGIN.EditorCMakeProjectSnapshot` version 2 is a tagged CMake File API/CTest
model. It contains safe preset descriptions, configured and stale state,
configurations, source/build directories, compiler identity/path metadata,
stable target IDs, compile groups, source ownership, dependencies, artifacts,
declaration backtraces, and discovered CTest names. It is not a Composition
Graph. Cache values, preset environment values, and arbitrary environment
state are excluded.

Omitting `--configure-preset` never configures and returns safe authored preset
metadata plus a degraded physical-navigation state. Configure, Build, and Test
are separate explicit commands. Editors must require workspace trust before
invoking them.

## Authoring plan

`NGIN.EditorAuthoringPlan` version 1 contains filesystem operations, minimal
offset-based manifest edits, before/after membership, matched rules,
diagnostics, affected products, refresh scopes, and manifest preconditions.
The request supplies desired paths; it does not encode a directory convention.

Version 1 implements `CreateItems`, `IncludeItems`, `ExcludeItems`,
`RenameItems`, `MoveItems`, `DeleteItems`, `AddPackage`,
`ChangePackageRequirement`, and `RemovePackage`. Each
`--item` uses `Kind|Visibility|relative-path`; Visibility may be empty. The
planner understands active-context `When` contributions, Include globs,
Exclude filters, and Remove rules. A new path already covered by a glob yields
no redundant exact Include.

Editors must:

1. preview plans that create several files or edit a manifest;
2. verify every precondition immediately before application;
3. reject a stale or dirty manifest rather than saving it implicitly; and
4. apply filesystem and text operations through the editor's workspace edit
   API so normal undo and dirty-document behavior is preserved.

Generated and external items cannot be moved or deleted. Delete also rejects
Stage and Action inputs until their owning authored role can be removed
explicitly. Rename and Move update exact authored path references and reject
nested-product boundary crossings.

Package requests use `--package` and optionally `--version` or `--exact`.
They only mutate direct requirements: transitive packages remain read-only and
retain graph provenance for Explain workflows.
