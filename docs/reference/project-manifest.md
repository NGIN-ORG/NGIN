# Project manifest reference

A `.nginproj` has one direct product root:

```xml
<Project Name="App" Type="Application" Version="1.0.0">
  <Dependencies><Package Name="NGIN.Base" Compatible="0.1" /></Dependencies>
  <Build><Source Include="src/**/*.cpp" /></Build>
  <Launch Name="default" Default="true"><Executable Product="App" /></Launch>
</Project>
```

`Name` and `Type` are required. `Version` and Library `Linkage` are domain
data. Authored manifests have no public format-number attribute.

## Direct sections

| Section | Meaning |
| --- | --- |
| `Metadata` | Description, license, homepage, and vendor |
| `Options` | Typed Boolean, Enum, String, Integer, and Path choices |
| `Dependencies` | Direct package and project dependencies |
| `Build` | Language and additive build items |
| `Generate` | Explicit Generate Action selections |
| `Tooling` | Analyze, Format, Validate, and Custom Action selections |
| `Stage` | Project-owned files and directories |
| `Launch` | Named process launch intent |
| `Testing` | Arguments, timeout, and test-context dependencies |
| `Publish` | One named Folder, Archive, or Installer result |
| `Refinements` | Additions selected by Configuration, Target, Toolchain, or Options |

Build items are `Source`, `Header`, `CxxModule`, `Resource`,
`IncludeDirectory`, `Define`, `CompileOption`, `LinkOption`,
`PrecompiledHeader`, `UnityBuild`, and `Convention`. Paths use `/`, are relative
to their owning manifest, and are normalized before graph identity is formed.

Version constraints use `Exact`, `Compatible`, or a structured `<Version>`
with `AtLeast`, `After`, `AtMost`, and `Before`; comparison operators are never
encoded in XML attribute text. Placeholders use `${namespace.name}` and are
validated for the phase in which they are expanded.

The generated structural schema and CLI semantic validation are authoritative:

```bash
ngin schema --format json
ngin validate --project App.nginproj
```
