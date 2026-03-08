# Workspace Metadata

`Workspace/` contains umbrella-repo metadata used by the NGIN development workspace.

- `Releases/` contains workspace release manifests and component pinning data.
- `Catalogs/` contains local package lookup metadata used by the workspace CLI.

These files are not the public package contract. Package dependencies and runtime/build contribution metadata live in `.nginpkg` files under [Packages](/home/berggrenmille/NGIN/Packages).
