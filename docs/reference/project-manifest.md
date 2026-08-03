# Project manifest reference

A `.nginproj` is an XML document with a `Project` root:

```xml
<Project SchemaVersion="4"
         Name="App"
         Version="1.0.0"
         DefaultProfile="Debug">
  <Application />
</Project>
```

`SchemaVersion` and `Name` are required. `Version` and `DefaultProfile` are
optional unless a selected feature, such as installer publishing, requires
them.

The CLI's executable schema is authoritative:

```bash
ngin schema --format json
ngin validate --project App.nginproj --profile Debug
```

## Root sections

| Section | Purpose |
| --- | --- |
| `Conditions` | Named selection expressions |
| `Defaults` | Project-wide language, backend, and selection defaults |
| Product element | One primary product and its behavior |
| `Profile` | Project defaults and product overlay |

Exactly one primary product element is used: `Application`, `Library`, `Tool`,
`Test`, `Benchmark`, `Plugin`, `Module`, or `External`.

## Product sections

Product kinds expose the sections that apply to them. Common sections are:

| Section | Contents |
| --- | --- |
| `Uses` | Project, package, tool, and runtime dependencies |
| `Build` | Language, sources, headers, include paths, definitions, and options |
| `Generate` | Project generators |
| `Stage` | Config, content, and runtime files |
| `Runtime` | Runtime module declarations |
| `Environment` | Public and secret environment values |
| `Tooling` | Named quality and development-tool runs |
| `Launch` | Executable, working directory, arguments, and environment |
| `Publish` | Folder, archive, or installer output |
| `Exports` | Public library or module contract |
| `PackageOutput` | Package produced by this project |

Product behavior belongs inside the product element, not as a parallel
root-level build model.

## Profiles

```xml
<Profile Name="Debug">
  <Defaults>
    <Optimization Mode="Off" />
    <DebugSymbols Enabled="true" />
    <LinkTimeOptimization Enabled="false" />
    <TargetPlatform Name="host" />
    <Environment Name="development" />
  </Defaults>
  <Application>
    <!-- overlay for the primary Application -->
  </Application>
</Profile>
```

A profile overlay uses the same product kind as the primary product. Named
items merge by their stable identity. The schema defines which items may be
removed, replaced, or extended.

## Selectors and conditions

Selectable items may use `Profile`, `Platform`, `OperatingSystem`,
`Architecture`, `Toolchain`, `Environment`, or `When`. Named conditions support
`All`, `Any`, `Not`, `When`, and `ConditionRef` expressions.

## Resolution

Paths are interpreted relative to the manifest that owns them unless the item
contract states otherwise. Variables use `$(Name)` syntax. Secrets are redacted
from graph, explain, diagnostics, and generated launch output.

Use `ngin graph`, `ngin inspect --format json`, `ngin diff`, and
`ngin explain <kind>:<identity>` to inspect the resolved result.
