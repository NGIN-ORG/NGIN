# Generators

Generators turn package-provided or project-provided host tools into declared
build steps. They name their inputs, outputs, arguments, and tool origin so the
Composition Graph can explain and schedule them.

The project explicitly selects a package-exported Action:

```xml
<Project Name="Hello.Reflection" Type="Application">
  <Dependencies>
    <Package Name="NGIN.Reflection" Compatible="0.1" />
    <Package Name="NGIN.Reflection.MetaGen" Compatible="0.1">
      <Use Action="ReflectionCodegen" />
    </Package>
  </Dependencies>
  <Build><Source Include="src/**/*.cpp" /></Build>
  <Generate Action="NGIN.Reflection.MetaGen::ReflectionCodegen" />
</Project>
```

The package owns the Tool, arguments, and generated output contract. The
project deliberately activates the Action; package presence never executes it.

Generated files are typed graph inputs. NGIN reruns a command generator when
its context, executable, arguments, declared inputs, source state, or required
outputs change. Unchanged generated content keeps its timestamp.

Inspect a selected generator with:

```bash
ngin explain action:NGIN.Reflection.MetaGen::ReflectionCodegen
ngin graph --format json
```

For a complete working example, see
[Hello.Reflection](../../Examples/Hello.Reflection). Package authors should use
the [package manifest reference](../reference/package-manifest.md).
