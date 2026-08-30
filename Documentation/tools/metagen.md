---
title: NGIN.Reflection.MetaGen
description: Select reflection generation as project intent and compile its generated registration normally.
---

# NGIN.Reflection.MetaGen

MetaGen is a package-provided host tool. Projects select its generation action;
package presence alone does not run it.

```xml
<Generate Using="NGIN.Reflection.MetaGen/ReflectionCodegen" Version="0.1">
  <Header Include="src/**/*.hpp" />
</Generate>
```

## Host and target contexts

The generator executable runs in host context. Its generated C++ belongs to the
target product context. Keeping those contexts distinct matters for cross
compilation.

## Build integration

Normal builds execute selected Generate actions. Inputs, outputs, tool identity,
and relevant compile context are recorded in the resolved graph so generation
is reproducible and inspectable.

Generated files are disposable build output. Change annotations, generator
options, or the action contract rather than editing generated registration.

See the [reflection MetaGen guide](../libraries/reflection/metagen.md) for the
complete flow.
