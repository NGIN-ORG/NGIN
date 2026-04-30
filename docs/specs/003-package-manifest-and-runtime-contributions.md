# Spec 003: Package Manifest and Runtime Contributions

Status: Active
Last updated: 2026-04-30

## Purpose

This spec defines the active V3 `.nginpkg` contract.

Packages are the reusable unit in NGIN.

Packages contribute to composition through declared manifest sections rather than through open-ended side effects.

## File Contract

- filename: `<PackageName>.nginpkg`
- root element: `<Package>`
- required root attributes:
  - `SchemaVersion="3"`
  - `Name`
  - `Version`

## Supported Top-Level Sections

All supported top-level sections are optional unless a narrower section contract
explicitly says otherwise.

- `Dependencies`
- `Artifacts`
- `Build`
- `Tools`
- `Modules`
- `Plugins`
- `Inputs`
- `Bootstrap`
- `Features`
- `PackagePolicy`

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

NGIN does not define a separate top-level `<Capabilities>` schema. Package
features are the public opt-in unit; capabilities are metadata declared by
features.

## Features

Packages may declare explicit opt-in features under `Features`.

```xml
<Features>
  <Feature Name="Reflection" Description="Enable reflection metadata support.">
    <Provides>
      <Capability Name="Reflection" />
    </Provides>
    <Dependencies>
      <PackageRef Name="NGIN.Reflection" VersionRange=">=0.1.0 &lt;0.2.0" />
    </Dependencies>
    <Inputs>
      <Configs>
        config/reflection.cfg
      </Configs>
    </Inputs>
    <Build>
      <CompileDefinitions>
        <Definition Value="NGIN_CORE_FEATURE_REFLECTION" Visibility="Public" />
      </CompileDefinitions>
    </Build>
    <Generators>
      <Generator Name="ReflectionMetaGen" Kind="Command" Tool="MetaGen">
        <Arguments>
          <Arg Value="--context" />
          <Arg Path="$(GeneratorContext)" />
        </Arguments>
        <Outputs>
          <Generated Role="Source"
                     Path="$(GeneratedDir)/reflection/$(ProjectName).reflection.generated.cpp" />
        </Outputs>
      </Generator>
    </Generators>
    <Runtime>
      <EnableModules>
        <ModuleRef Name="NGIN.Reflection" />
      </EnableModules>
    </Runtime>
    <Variables>
      <Variable Name="NGIN_REFLECTION" Value="1" />
    </Variables>
  </Feature>
</Features>
```

Feature contributions are selected only when a project, profile, or environment
uses the feature. Package defaults are explicit-only in Phase D; package
features never auto-apply.

Supported feature contributions are package refs, typed inputs, generators,
build settings, runtime declarations, environment variables, and capability
metadata. Capability requirements are validated after selected feature closure.
Multiple providers for the same capability are allowed unless a provider declares
`Exclusive="true"`.

Package policy is explicit:

```xml
<PackagePolicy DefaultFeatures="Explicit" LockFile="Optional" />
```

## Inputs

Packages use the same normalized `Inputs` surface as projects. Authored
`Contents` is removed.

```xml
<Inputs>
  <Contents ContentKind="config">
    <File Path="config/defaults.xml"
          Target="config/defaults.xml" />
  </Contents>
</Inputs>
```

Supported package input blocks are `Configs`, `Contents`, `Assets`,
`Generated`, and `ToolInputs`. Package configs, contents, assets, and generated
staged roles flow through the common input pipeline; tool inputs are validated
and exposed as metadata only.

## Tools And Generators

Packages may declare named tools for selected feature generators:

```xml
<Tools>
  <Tool Name="MetaGen" Kind="Generator" Executable="bin/ngin-metagen" />
  <Tool Name="SchemaCompiler" Kind="Generator" Executable="bin/schema-compiler" />
</Tools>
```

Tool paths resolve relative to the provider root when one is present; otherwise
they resolve relative to the package manifest directory. Phase E supports
executable generator tools only; MetaGen is provided by the
`NGIN.Reflection.MetaGen` package as `bin/ngin-metagen`, not by the CLI.

Feature-contributed generators apply only when the consuming project selects
that package feature. Generator outputs must be explicit and become typed
`Generated` inputs in the selected build.

## Conditions

Packages may declare package-local conditions under `Conditions`. Package
conditions can reference built-in conditions and other package-local conditions.
They apply to package inputs, package runtime module declarations, and package
plugin declarations. Project manifests cannot reference package-local condition
names.

```xml
<Conditions>
  <Condition Name="DesktopTools">
    <ConditionRef Name="Desktop" />
  </Condition>
</Conditions>
<Inputs>
  <ToolInputs Condition="DesktopTools">
    tools/schema.json
  </ToolInputs>
</Inputs>
```

## Ownership Rule

Packages describe reusable identity and exposed behavior.

Workspace-local source ownership is resolved outside the package manifest through:

- workspace `PackageSources`
- workspace `PackageProviders`
- package manifest location when no provider override exists
