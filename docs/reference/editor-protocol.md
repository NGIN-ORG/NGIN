# Editor protocol

The native `ngin` CLI owns the semantic contract used by editors. Version 1
exposes workspace and resolved product snapshots and plans authoring changes
without applying them:

```text
ngin editor workspace --workspace NGIN.ngin
ngin editor snapshot --project App.nginproj --configuration Debug
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

`NGIN.EditorWorkspaceSnapshot` version 1 contains authored workspaces, their
stable identities and boundaries, discovered products, standalone products,
and diagnostics. Editors use it for discovery and keep physical navigation
available when an individual product graph is degraded.

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
