# Project manifest authoring

An `.nginproj` describes one primary product and the behavior needed to build,
stage, and use it. This page is the short map; the focused guides explain each
part.

```xml
<Project SchemaVersion="4" Name="App" DefaultProfile="Debug">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Launch Executable="$(OutputName)" />
  </Application>

  <Profile Name="Debug">
    <Defaults>
      <Optimization Mode="Off" />
      <DebugSymbols Enabled="true" />
      <LinkTimeOptimization Enabled="false" />
      <TargetPlatform Name="host" />
    </Defaults>
  </Profile>
</Project>
```

The product element is one of `Application`, `Library`, `Tool`, `Test`,
`Benchmark`, `Plugin`, `Module`, or `External`. Product behavior belongs inside
that element. Profiles may add or replace behavior with an element of the same
product kind.

Read next:

- [Projects](projects.md) for product structure and build inputs.
- [Profiles](profiles.md) for selection and overlays.
- [Packages](packages.md) for dependencies and features.
- [Generators](generators.md) for code generation.
- [Staging and launch](staging-and-launch.md) for runnable output.
- [Publishing](publishing.md) for distributable output.
- [Project manifest reference](../reference/project-manifest.md) for the
  contract summary.

Use `ngin validate` while authoring and `ngin schema --format json` when you
need the exact schema supported by the CLI you are running.
