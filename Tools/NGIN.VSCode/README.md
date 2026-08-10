# NGIN for VS Code

This deliberately small extension improves the XML authoring loop without
implementing a second project model.

It provides:

- context-aware element and attribute completion generated from `ManifestSpec`;
- project, package, workspace, and CMake-integration XSD artifacts;
- `NGIN: Validate Manifest` through the native CLI's structural and semantic validators;
- `NGIN: Format Manifest` through the comment-preserving formatter;
- `NGIN: Show Manifest Schema Metadata` for inspecting the live vocabulary.

Configure `ngin.executable` if `ngin` is not on `PATH`. Validation on save is
controlled by `ngin.validateOnSave`.

The extension contains no manifest parser, profile model, feature editor,
build resolver, or backend logic. The CLI and immutable Composition Graph stay
authoritative, which prevents editor behavior from drifting from command-line
and CI behavior.

Build it with:

```bash
npm install
npm run typecheck
npm run build
```

Regenerate `schemas/` with the `ngin_manifest_schema_generator` CMake target
whenever `ManifestSpec` changes.
