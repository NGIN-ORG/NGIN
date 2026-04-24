# Spec 003: Package Manifest and Runtime Contributions

Status: Active
Last updated: 2026-04-24

## Purpose

This spec defines the active V2 `.nginpkg` contract.

Packages are the reusable unit in NGIN.

Packages contribute to composition through declared manifest sections rather than through open-ended side effects.

## File Contract

- filename: `<PackageName>.nginpkg`
- root element: `<Package>`
- required root attributes:
  - `SchemaVersion="2"`
  - `Name`
  - `Version`

## Supported Top-Level Sections

All supported top-level sections are optional unless a narrower section contract
explicitly says otherwise.

- `Dependencies`
- `Artifacts`
- `Build`
- `Modules`
- `Plugins`
- `Contents`
- `Bootstrap`

`SourceBinding` is removed from the active package contract.

## Dependency Surface

Packages continue to use `PackageRef` inside `Dependencies`:

```xml
<Dependencies>
    <PackageRef Name="NGIN.Base" VersionRange=">=0.1.0 &lt;0.2.0" Optional="false" />
</Dependencies>
```

## Versioning Rule

- package manifests carry an explicit package identity of `Name` plus `Version`
- one resolved version per package identity may participate in a single composition
- version resolution behavior beyond that invariant is defined by the resolver and workspace policy, but compositions must not contain ambiguous multi-version results for the same package identity

## Contribution Surface

The active capability surface of a package is defined by the supported top-level manifest sections in this spec. Tooling and validation should treat those sections as the declared contribution types of the package.

Package dependencies and runtime module selection are separate concepts. Declaring
a package dependency makes that package's exported artifacts available to the
consumer, such as link libraries and their headers. It does not imply that the
package contributes or enables a runtime module.

Package dependencies use `PackageRef`. Module dependency descriptors may still
use `Dependency` under module-specific sections; those are runtime module edges,
not package references.

`Modules` is reserved for runtime participants that the host should reason about
as lifecycle, dependency-order, service, or capability nodes. Library-only
packages should use `Artifacts` without declaring placeholder modules.

At the current V2 stage, NGIN does not define a second parallel `<Capabilities>` schema. Capability clarity should come from explicit manifest sections, validation, and graph or inspection tooling.

## Ownership Rule

Packages describe reusable identity and exposed behavior.

Workspace-local source ownership is resolved outside the package manifest through:

- workspace `PackageSources`
- workspace `PackageProviders`
- package manifest location when no provider override exists
