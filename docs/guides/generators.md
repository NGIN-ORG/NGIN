# Generators

Select a generator once at the point of use:

```xml
<Executable Name="Hello.Reflection">
  <Uses><Package Name="NGIN.Reflection" Version="0.1" /></Uses>
  <Build><Source Include="src/**/*.cpp" /></Build>
  <Generate Using="NGIN.Reflection.MetaGen/ReflectionCodegen" Version="0.1">
    <Header Include="src/**/*.hpp" />
  </Generate>
</Executable>
```

Generate selection introduces the generator package, action export, and
backing Tool in host context. Do not add the Tool package to Uses solely to
activate it. Generation is deterministic only when the package action declares
that contract and workspace trust permits execution. Generated outputs enter
Manifest IR and the Composition Graph with provenance.
