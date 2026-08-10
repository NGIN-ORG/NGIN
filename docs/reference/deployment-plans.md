# Deployment, launch, test, and publish plans

NGIN resolves authored XML once and then derives typed plans from the immutable
Composition Graph. Executors receive a plan, never an authored XML node.

## StagePlan

StagePlan combines the primary product artifact, active Plugin artifacts,
project `Stage` content, package runtime files and assets, legal notices, and
debug-symbol artifact bindings.
Artifact locations and package roots are external bindings keyed by graph
identity. Absolute local paths may therefore enter the executor plan without
making the semantic graph machine-dependent.

Every item records an owner and reason. Missing sources, unreadable directories,
escaping destinations, symlink escapes, target case collisions, and destination
collisions fail plan derivation. Replacement is permitted only when policy names
the exact destination and new owner. Plugin staging deploys the artifact; it
never implies loading, service registration, lifecycle ordering, or runtime
module configuration.

The stage executor accepts only StagePlan. It rechecks destination containment
before copying and refuses unresolved symbolic artifacts.

## LaunchPlan

A Launch with no explicit `Executable` selects the primary Product. An explicit
Tool must name an active Tool Export. LaunchPlan contains the executable,
repeated arguments in authored order, a safe working directory, environment,
external secret references, runtime-library search paths, and process
prerequisites.

Windows augments `PATH`, macOS/iOS augments `DYLD_LIBRARY_PATH`, and other
targets use `LD_LIBRARY_PATH`. Existing authored values are preserved after the
staged library directory. Secret contents are resolved only by the process
executor and never enter graph or plan serialization.

## TestPlan

TestPlan uses the staged product artifact, Testing arguments and timeout, and
only dependency edges in the `Test` context. Test and Benchmark products receive
default testing intent without a `Testing` section. Testing dependencies do not
leak into ordinary target or Publish plans.

## PublishPlan

PublishPlan selects one named backend-neutral Folder, Archive, or Installer
intent. It receives the deterministic StagePlan inventory and classifies each
input as Product, Runtime, Plugin, Asset, Notice, or project content. Inputs
retain ownership and reason. Project license metadata and dependencies scoped
to the selected Publish are explicit plan fields.

The current CPack adapter maps `zip`, `tgz`, `msi`, and `deb` only after the
PublishPlan exists. CPack variables and commands never appear in authored XML or
the Composition Graph. Folder publication can copy the typed inventory
directly; Archive and Installer adapters use the same ordered inventory.

## Fingerprints

StagePlan, LaunchPlan, TestPlan, and PublishPlan each have their own SHA-256
fingerprint. The fingerprint includes executor bindings relevant to that plan,
while the graph's composition fingerprint remains backend-neutral.
