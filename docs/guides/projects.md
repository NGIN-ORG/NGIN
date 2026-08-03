# Projects

One `.nginproj` describes one primary product. This keeps the product identity
clear and lets the resolved Composition Graph drive builds, editors, staging,
tests, and publishing from the same information.

## Choose a product kind

Use the element that describes what the project produces:

| Kind | Typical output |
| --- | --- |
| `Application` | Runnable application |
| `Library` | Reusable library |
| `Tool` | Host or developer executable |
| `Test` | Test executable |
| `Benchmark` | Benchmark executable |
| `Plugin` | Loadable plugin |
| `Module` | Runtime or library module |
| `External` | Product owned by an external build |

## Describe the build

Build inputs belong inside the product:

```xml
<Library Output="Static">
  <Build>
    <Language Standard="C++23" Required="true" Extensions="false" />
    <Sources Path="src/**.cpp" />
    <Headers Path="include/**.hpp" Visibility="Public" />
    <IncludePath Path="include" Visibility="Public" />
    <Define Name="MATH_BUILDING" Value="1" />
  </Build>

  <Exports>
    <LibraryTarget Name="Math::Math" />
    <Headers Path="include/**.hpp" />
  </Exports>
</Library>
```

`Sources` and `Headers` declare product inputs. Build settings such as
`IncludePath`, `Define`, `CompileOption`, `LinkOption`, and `LinkLibrary` can
carry selectors such as `Profile`, `OperatingSystem`, `Architecture`,
`Toolchain`, `Environment`, or a named `When` condition.

## Add a condition

Use a named condition when several items share the same selection rule:

```xml
<Conditions>
  <Condition Name="windows-debug">
    <All>
      <When OperatingSystem="windows" />
      <When Profile="Debug" />
    </All>
  </Condition>
</Conditions>

<Application>
  <Build>
    <Define Name="APP_WINDOWS_DEBUG" Value="1" When="windows-debug" />
  </Build>
</Application>
```

## Inspect the result

```bash
ngin validate
ngin graph
ngin inspect --format json
```

The manifest records intent. The Composition Graph is the resolved result after
profiles, workspace policy, packages, conditions, and defaults are applied.

See [Profiles](profiles.md) next, or open the exact
[project manifest reference](../reference/project-manifest.md).
