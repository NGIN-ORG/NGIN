# Generators

Generators turn package-provided or project-provided host tools into declared
build steps. They name their inputs, outputs, arguments, and tool origin so the
Composition Graph can explain and schedule them.

The usual path is to select a package feature that contributes the generator:

```xml
<Application>
  <Uses>
    <Package Name="NGIN.Reflection.MetaGen"
             Version=">=0.1.0 &lt;0.2.0"
             Scope="Build">
      <Feature Name="ReflectionCodegen" />
    </Package>
  </Uses>
  <Build>
    <Sources Path="src/**.cpp" />
  </Build>
</Application>
```

The package owns the executable, arguments, and generated output declaration.
The project only opts into the feature.

Generated files are typed graph inputs. NGIN reruns a command generator when
its context, executable, arguments, declared inputs, source state, or required
outputs change. Unchanged generated content keeps its timestamp.

Inspect a selected generator with:

```bash
ngin explain generator ReflectionMetaGen
ngin graph --format json
```

For a complete working example, see
[Hello.Reflection](../../Examples/Hello.Reflection). Package authors should use
the [package manifest reference](../reference/package-manifest.md).
