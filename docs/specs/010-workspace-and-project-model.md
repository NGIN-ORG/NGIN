# Spec 010: Workspace and Project Model

Status: Active
Last updated: 2026-04-28

## Purpose

This spec defines the authored model split between workspace, project, and package.

## Rules

- `.nginproj` is the normal authored unit for applications, tools, and libraries
- `.nginpkg` is the reusable dependency unit
- `.ngin` is optional
- `.nginmodel` is the shared model unit for defaults, platforms, project
  templates, and profile templates
- separate executables should usually be separate projects
- profiles should hold narrow selection and override data
- NGIN does not model application category as a profile property
- launch metadata belongs to launchable projects

## Workspace V3

Workspace manifests use `SchemaVersion="3"`.

```xml
<Workspace SchemaVersion="3" Name="Examples">
  <Includes>
    <Include Path="Common.nginmodel" />
  </Includes>
  <Defaults BuildType="Debug" Platform="linux-x64" />
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
</Workspace>
```

Workspace defaults apply to projects loaded through workspace resolution.
Project-local defaults and explicit project/profile values override workspace
defaults.

Workspaces may centralize package version ranges and package policy:

```xml
<DependencyPolicy VersionResolution="HighestCompatible">
  <Versions>
    <Package Name="NGIN.Core" VersionRange=">=0.1.0 &lt;0.2.0" />
  </Versions>
</DependencyPolicy>
<PackagePolicy DefaultFeatures="Explicit" LockFile="Optional" />
```

Project dependency policy overrides workspace policy by package name. Phase D
supports explicit package features only; package defaults do not auto-apply.

## Shared Model Files

Shared model files use the `.nginmodel` extension and a `Model` root.

```xml
<Model SchemaVersion="3" Name="Common">
  <Includes>
    <Include Path="Platforms.nginmodel" />
  </Includes>
  <Defaults BuildType="Debug" Platform="linux-x64" />
  <Conditions>
    <Condition Name="HostedDebug" BuildType="Debug" Environment="local" />
  </Conditions>
  <ProjectTemplates>
    <ProjectTemplate Name="ConsoleTool" Type="Tool" OutputKind="Executable" />
  </ProjectTemplates>
  <ProfileTemplates>
    <ProfileTemplate Name="LocalDebug" Environment="local">
      <Launch Executable="$(OutputName)" WorkingDirectory="." />
    </ProfileTemplate>
  </ProfileTemplates>
</Model>
```

Includes are resolved relative to the declaring file, depth-first, in
declaration order. Missing include files are validation errors. Include cycles
are validation errors and diagnostics include the cycle chain.

The effective model layer order is:

1. built-in model catalog
2. workspace includes
3. workspace declarations
4. project includes
5. project declarations

Built-in platforms are `linux-x64`, `windows-x64`, `macos-x64`, and
`macos-arm64`. Built-in project templates are `Application`, `Library`, and
`Tool`.

Built-in conditions are part of the model catalog. Workspace and shared model
files may add authored conditions, and project-local conditions append after
project includes. Duplicate authored condition names are validation errors, and
authored conditions may not replace built-in condition names.

Collection declarations append in resolution order. Duplicate authored names for
platforms, project templates, and profile templates are validation errors, except
that authored declarations may intentionally replace built-in catalog entries.

## Consequences

- engine library, game client, and headless server are usually separate projects
- `Debug`, `Shipping`, `Diagnostics`, and similar modes are profiles
- package-local source binding is not part of the active reusable contract
