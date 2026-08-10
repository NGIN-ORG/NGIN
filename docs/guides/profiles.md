# Configurations, targets, toolchains, and presets

NGIN does not use semantic profiles. A build selection has three named facts:
Configuration, Target, and Toolchain, plus declared typed Options. This avoids
one profile name secretly changing unrelated build, runtime, and packaging
behavior.

Workspaces declare reusable choices and defaults:

```xml
<Workspace Name="Example">
  <Configurations>
    <Configuration Name="Debug"><Optimization Mode="Off" /><DebugSymbols Enabled="true" /></Configuration>
    <Configuration Name="Release"><Optimization Mode="Speed" /><DebugSymbols Enabled="false" /></Configuration>
  </Configurations>
  <Targets><Target Name="host" OS="host" Architecture="host" /></Targets>
  <Toolchains><Toolchain Name="default" Compiler="default" /></Toolchains>
  <Defaults>
    <Configuration Name="Debug" />
    <Target Name="host" />
    <Toolchain Name="default" />
  </Defaults>
  <Presets>
    <Preset Name="release" Command="build"><Configuration Name="Release" /></Preset>
  </Presets>
</Workspace>
```

Projects use typed Options for real product choices and `<Refinement>` for
selection-specific additions. Presets only expand command inputs; their names
never enter graph or artifact identity. See the [workspace reference](../reference/workspace-manifest.md)
and [selection model](../reference/manifest-authoring-model.md#selection-model).
