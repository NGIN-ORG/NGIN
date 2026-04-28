# NGIN Local Settings And Secrets Plan

## Summary

NGIN projects need a way to declare required local values, machine-specific
settings, and secrets without committing those values into project manifests.

## Implementation Status

Implemented on 2026-04-28.

The v1 policy is:

- repository-local settings are loaded only through explicit project imports
- user-global settings at `~/.ngin/settings.nginsettings` are an inert fallback
  for variables that explicitly use `FromLocalSetting`
- `ngin variables explain` is the first inspection command
- `ngin settings init` initializes the default ignored local settings file
- project manifest variables using `Secret="true"` may not use literal `Value`

The active project model already has `Environments` and environment
`Variables`, but those are authored project data. They are appropriate for
committed defaults and non-secret runtime values. They are not enough for local
developer secrets, private feed credentials, SDK paths, signing keys, or CI-only
values.

This plan introduces a strict separation between:

- committed project variable declarations
- ignored repository-local settings
- user-global settings outside the repository
- operating system environment variable imports
- future secret-provider imports

The core rule is:

> Project manifests declare what value is needed and where it comes from.
> Local and global settings only provide values when explicitly selected. They
> do not participate in condition evaluation or change the build graph shape.

Structured conditions and typed source selectors should be settled first. This
plan should not expand the first condition model to depend on arbitrary local or
secret values.

## Goals

- Keep secrets out of committed `.nginproj`, `.ngin`, and `.nginpkg` files.
- Let projects declare that a value is required without storing the value.
- Make each project variable choose an explicit value source.
- Support local machine-specific values through ignored repository-local
  settings.
- Support user-global settings outside the repository.
- Support CI-friendly environment variable imports.
- Preserve deterministic diagnostics without printing secret values.
- Provide inspection tooling so users can see where resolved values came from.
- Support IDE workflows for initializing, inspecting, and safely authoring local
  settings.
- Keep local settings separate from condition selection in the first
  implementation.

## Non-Goals

- Do not implement a full secret vault in the first version.
- Do not make builds depend on arbitrary local variables for condition matching.
- Do not use broad global precedence across all variables.
- Do not store secret values in lock files, launch manifests, generated CMake,
  generated source, logs, graph output, or diagnostics.
- Do not store secret values in VS Code workspace state, extension logs, launch
  configurations, or normal VS Code settings.
- Do not require `NGIN.Core` for CLI-side local setting resolution.
- Do not replace committed project `Environments` or `Variables`.
- Do not make the VS Code extension the authoritative variable resolver.

## User-Facing Model

A committed project manifest can declare required values without embedding the
secret:

```xml
<LocalSettings>
  <Import Path=".ngin/local/user.nginsettings" Optional="true" />
</LocalSettings>

<Environments>
  <Environment Name="development">
    <Variables>
      <Variable Name="NGIN_API_ENDPOINT"
                Value="https://dev.example.com" />

      <Variable Name="NGIN_API_TOKEN"
                FromLocalSetting="feeds.private.token"
                Required="true"
                Secret="true" />

      <Variable Name="VULKAN_SDK"
                FromLocalSetting="sdk.vulkan.root"
                Required="true" />
    </Variables>
  </Environment>
</Environments>
```

The project declares what variables it needs and which source family should
provide each value.

CI-oriented variables can read from the operating system environment:

```xml
<Variable Name="NGIN_API_TOKEN"
          FromEnvironment="NGIN_API_TOKEN"
          Required="true"
          Secret="true" />
```

Machine-specific values can live in an ignored local settings file:

```text
.ngin/local/user.nginsettings
```

Example local settings file:

```xml
<?xml version="1.0" encoding="utf-8"?>
<LocalSettings SchemaVersion="1">
  <Settings>
    <Setting Key="feeds.private.token"
             Value="local-development-token"
             Secret="true" />

    <Setting Key="sdk.vulkan.root"
             Value="/home/user/vulkan-sdk" />
  </Settings>
</LocalSettings>
```

User-global settings can live outside the repository:

```text
~/.ngin/settings.nginsettings
```

These are useful for SDK paths, private package feed credentials, signing
identity names, and other values shared across multiple local workspaces.

## Variable Sources

The first variable source set should support:

- `Value`
- `FromEnvironment`
- `FromLocalSetting`

Examples:

```xml
<Variable Name="NGIN_API_ENDPOINT"
          Value="https://dev.example.com" />
```

```xml
<Variable Name="VULKAN_SDK"
          FromEnvironment="VULKAN_SDK"
          Required="true" />
```

```xml
<Variable Name="PrivateFeedToken"
          FromLocalSetting="feeds.private.token"
          Required="true"
          Secret="true" />
```

`Value` stores a literal value in the current manifest or local settings file.

`FromEnvironment` reads one operating system environment variable.

`FromLocalSetting` reads one key from loaded repository-local or user-global
settings.

Exactly one value source should be allowed per project `Variable` item.

A variable declared as `FromEnvironment` should only read from the operating
system environment. A variable declared as `FromLocalSetting` should only read
from local settings. Environment variables should not implicitly override local
settings unless a future explicit source mode is designed.

## Variables Vs Settings

Project variables and local settings use different namespaces.

| Concept | Example | Purpose |
| --- | --- | --- |
| Project/runtime variable name | `NGIN_API_TOKEN` | The variable NGIN resolves for the project and may pass to tools or runtime |
| Local setting key | `feeds.private.token` | The private lookup key used to find a value in local or user-global settings |

The names may match, but they do not have to:

```xml
<Variable Name="NGIN_API_TOKEN"
          FromLocalSetting="feeds.private.token"
          Required="true"
          Secret="true" />
```

Local settings files should therefore use `<Setting Key="...">`, not
`<Variable Name="...">`.

## Local Setting Files

Local setting files should use a small NGIN-owned XML format:

```xml
<?xml version="1.0" encoding="utf-8"?>
<LocalSettings SchemaVersion="1">
  <Settings>
    <Setting Key="feeds.private.token"
             Value="..."
             Secret="true" />

    <Setting Key="sdk.vulkan.root"
             Value="/opt/vulkan-sdk" />
  </Settings>
</LocalSettings>
```

Recommended paths:

- repository-local: `.ngin/local/user.nginsettings`
- user-global: `~/.ngin/settings.nginsettings`

Repository-local files under `.ngin/local/` should be ignored by source control.
The CLI should warn if a known local settings file appears tracked by git.

Repository-local settings should require explicit import in v1:

```xml
<LocalSettings>
  <Import Path=".ngin/local/user.nginsettings" Optional="true" />
</LocalSettings>
```

User-global settings may be auto-loaded later, but only as an inert source for
variables that explicitly use `FromLocalSetting`. If user-global settings are
auto-loaded in v1, inspection tooling should ship in the same milestone.

## Resolution Model

Variable resolution should be deterministic and source-directed.

For each project variable:

1. Validate that it has exactly one source selector:
   - `Value`
   - `FromEnvironment`
   - `FromLocalSetting`
2. Resolve according to that source selector only.
3. If the source is `FromLocalSetting`, check imported repository-local settings
   first, then enabled user-global settings.
4. If `Required="true"` and no value is found, fail before generating build,
   launch, or runtime outputs.
5. If `Secret="true"`, redact the value in every user-visible output and do not
   serialize the raw value into generated files.

There is no broad global precedence ladder across all variables in v1.

Precedence applies only inside a selected source family. For example,
`FromLocalSetting="sdk.vulkan.root"` may resolve repository-local settings
before user-global settings. `FromEnvironment="VULKAN_SDK"` should check only
the operating system environment.

## Secret Handling

`Secret="true"` marks a value as sensitive.

Secret values must not be emitted into:

- generated CMake files
- compile definitions
- generated source files
- lock files
- `.nginlaunch` files
- logs
- graph output
- CLI diagnostics
- future inspect/explain output

Example diagnostic:

```text
missing required secret variable 'NGIN_API_TOKEN'
```

Not:

```text
NGIN_API_TOKEN was 'abc123...'
```

Literal secret values should not be allowed in committed manifests:

```xml
<!-- Invalid or at least a strong warning in committed manifests -->
<Variable Name="NGIN_API_TOKEN"
          Value="abc123"
          Secret="true" />
```

Committed manifests should instead use:

```xml
<Variable Name="NGIN_API_TOKEN"
          FromEnvironment="NGIN_API_TOKEN"
          Required="true"
          Secret="true" />
```

or:

```xml
<Variable Name="NGIN_API_TOKEN"
          FromLocalSetting="feeds.private.token"
          Required="true"
          Secret="true" />
```

Literal secret values should be allowed only in ignored repository-local files or
user-global settings files.

If a secret must be passed to a runtime process, the preferred transport should
be environment inheritance or a protected temporary runtime file. Raw secret
values should not be serialized into launch manifests by default.

## Interaction With Conditions

Local settings and secrets should not participate in the first condition
selection context.

Conditions should initially match stable authored selection state such as:

- `Configuration`
- `BuildConfiguration`
- `OperatingSystem`
- `Architecture`
- `Environment`

This avoids making build graph shape depend on untracked local state.

Do not allow local settings or resolved variables to drive conditions in v1:

```xml
<!-- Deferred -->
<When LocalSetting="some.feature.enabled" Equals="true" />

<!-- Deferred -->
<When Variable="PRIVATE_FEED_ENABLED" Equals="true" />
```

Later, NGIN may support explicit non-secret local feature values as condition
inputs, but that should be a separate design with strong diagnostics and
reproducibility rules.

## CLI Behavior

The CLI should:

- load repository-local settings only when explicitly imported
- load user-global settings only when policy says they are enabled
- make loaded settings inert unless a variable uses `FromLocalSetting`
- validate required variables before build or launch generation
- redact secret values in output
- report unknown imports unless `Optional="true"`
- report conflicting variable source attributes on one item
- warn when known repository-local settings files are tracked by git
- provide an inspect command to show where resolved variables came from

The first inspection command should answer what the selected project actually
resolved:

```bash
ngin variables explain --project Examples/App.Basic/App.Basic.nginproj --configuration Runtime
```

Example output:

```text
NGIN_API_ENDPOINT = https://dev.example.com    source: project Value
NGIN_API_TOKEN    = <secret>                   source: local setting feeds.private.token
VULKAN_SDK        = /opt/vulkan-sdk            source: local setting sdk.vulkan.root
```

A broader settings command can be added later:

```bash
ngin settings list
```

Example output:

```text
sdk.vulkan.root      = /opt/vulkan-sdk       source: user-global
feeds.private.token  = <secret>              source: repo-local
```

The CLI should also eventually provide an initialization helper:

```bash
ngin settings init
```

That command can create `.ngin/local/user.nginsettings`, add `.ngin/local/` to
`.gitignore`, and optionally add an import to the project or workspace.

## VS Code Extension Integration

The VS Code extension should make local settings and secrets easier to discover
and safer to author, but the CLI remains authoritative for parsing, validation,
resolution, redaction, and diagnostics.

The extension should not reimplement variable resolution. It may parse enough
manifest structure to provide completions, snippets, and navigation, but final
answers should come from CLI commands such as `ngin validate` and
`ngin variables explain`.

Recommended first extension features:

- add `.nginsettings` as a recognized NGIN XML file type
- add snippets for `LocalSettings`, `Settings`, and `Setting`
- add snippets for project `Variable` entries using `Value`,
  `FromEnvironment`, and `FromLocalSetting`
- add a command equivalent to `ngin settings init`
- add a command that runs `ngin variables explain` for the active project and
  configuration
- surface missing required variables through existing diagnostics from
  `ngin validate`
- surface redacted variable explanation output in an output channel or document
- warn when `.ngin/local/` is not ignored after local settings are initialized
- warn when known repository-local settings files appear tracked by git

The initialize command should be safe by default. It can:

- create `.ngin/local/user.nginsettings` when missing
- add `.ngin/local/` to `.gitignore` when appropriate
- optionally add a project or workspace `<LocalSettings><Import ... />`
- avoid overwriting existing setting values

If the extension ever accepts secret values through UI prompts, it must avoid
writing those values to VS Code workspace state, extension logs, diagnostics,
launch configurations, or generated files. A later secret-provider milestone may
integrate VS Code `SecretStorage`, but that should be explicit provider
behavior rather than the default v1 local settings path.

The extension should treat all secret display as redacted:

```text
NGIN_API_TOKEN = <secret>    source: local setting feeds.private.token
```

Completions can be added incrementally:

- complete `FromLocalSetting` keys from loaded `.nginsettings` files
- complete `FromEnvironment` names from the current process environment only as
  a convenience, without storing values
- complete imported local setting file paths

Completions should not reveal secret values.

## Manifest Contract Changes

`docs/specs/002-project-and-target-manifest.md` should be updated to define:

- `LocalSettings` as an optional project root section
- `Import` entries with `Path` and `Optional`
- project `Variable` source attributes
- `Required`
- `Secret`

A local settings file contract should define:

- file extension `.nginsettings`
- root element `LocalSettings`
- required `SchemaVersion`
- `Settings`
- `Setting`
- `Key`
- `Value`
- `Secret`

Environment variables should continue to belong under project `Environments`.
The new work changes how variable values may be sourced, not the existence of
environment layers.

## Implementation Plan

1. Define the `.nginsettings` file format.
2. Add local settings model types to the CLI authoring model.
3. Parse project-level `<LocalSettings>` imports.
4. Parse imported repository-local `.nginsettings` files.
5. Decide user-global loading policy and parse user-global `.nginsettings`
   files only when enabled.
6. Extend environment variable parsing with `FromEnvironment`,
   `FromLocalSetting`, `Required`, and `Secret`.
7. Add source-directed variable resolution.
8. Add validation for missing required values and conflicting value sources.
9. Redact secret values in diagnostics and generated summaries.
10. Ensure generated launch manifests, CMake files, lock files, and graph output
    do not write raw secret values.
11. Add `ngin variables explain` or equivalent inspection.
12. Update VS Code extension snippets, file associations, and commands for
    initialization and variable explanation.
13. Update specs and README guidance.
14. Add targeted CLI tests for imports, source selection, local-setting
    precedence, required variables, and redaction.
15. Add targeted VS Code extension tests for command registration, snippets or
    file recognition, and redacted explanation display.

## Suggested Test Cases

- A required `FromEnvironment` variable resolves when the OS environment value
  is present.
- A required `FromEnvironment` variable fails validation when missing.
- A `FromEnvironment` variable does not resolve from local settings.
- A `FromLocalSetting` variable resolves from an imported repository-local
  setting.
- A `FromLocalSetting` variable resolves from enabled user-global settings when
  no repository-local value exists.
- Repository-local settings override user-global settings for `FromLocalSetting`.
- OS environment variables do not override `FromLocalSetting`.
- Secret values are redacted in diagnostics and inspection output.
- A missing optional local import is ignored.
- A missing required local import fails validation.
- A variable with both `Value` and `FromEnvironment` fails validation.
- A committed manifest with `Value` and `Secret="true"` fails validation or
  emits a strong warning.
- A secret value is not emitted into generated CMake, launch manifests, lock
  files, graph output, or compile definitions.
- The VS Code extension exposes a variables explanation command.
- The VS Code extension displays secret variables as `<secret>`.
- The VS Code extension initialization flow does not overwrite existing local
  setting values.
- The VS Code extension does not store secret values in workspace state.

## Open Questions

- Should user-global settings auto-load in v1, or require explicit workspace,
  project, or CLI policy?
- Should `ngin variables explain` be the first inspection command, with
  `ngin settings list` deferred?
- Should literal secrets in committed manifests be a hard error or a warning?
- Should settings support profiles, or should profiles be modeled as separate
  files imported by the project?
- How should secret provider plugins be discovered and trusted later?
- Should `ngin settings init` add imports at the project level or workspace
  level?
- Should the VS Code extension call `ngin settings init`, implement equivalent
  file edits itself, or support both depending on CLI availability?
- Should VS Code `SecretStorage` become a first-class secret provider in a later
  milestone?
