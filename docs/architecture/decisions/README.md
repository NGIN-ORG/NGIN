# Architecture decisions

These records preserve decisions that affect how users and contributors work
with NGIN today.

- [Use CMake as the generated backend](0001-cmake-generated-backend.md)
- [Keep project tooling separate from application runtime](0002-tooling-runtime-separation.md)
- [Describe one primary product per project](0003-one-product-per-project.md)
- [Keep UI backends separate from the UI core](0004-ui-backend-separation.md)
- [Resolve package instances and activate named exports](0005-package-instance-export-activation.md)
- [Use semantic merge laws and compact build selection](0006-semantic-merge-and-selection.md)
- [Keep backend bindings outside the Composition Graph](0007-backend-integration-boundary.md)
- [Separate dependency locks from composition fingerprints](0008-reproducibility-identities.md)

Implementation checklists and progress are tracked under `docs/plans/`. Git
history preserves superseded designs.
