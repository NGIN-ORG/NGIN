# Publishing

Publish intent is attached directly to a product:

```xml
<Executable Name="Gallery" Version="1.2.3">
  <Build><Source Include="src/**/*.cpp" /></Build>
  <Publish>
    <Archive Name="portable" Format="zip" Output="dist/gallery-1.2.3.zip" />
  </Publish>
</Executable>
```

`ngin publish portable` builds and stages the resolved product, derives a
PublishPlan, and invokes the current packaging backend. Publish intent remains
backend-neutral and does not execute arbitrary package scripts. Published NGIN
products emit portable CPS component metadata from the resolved product rather
than requiring parallel handwritten component metadata.
