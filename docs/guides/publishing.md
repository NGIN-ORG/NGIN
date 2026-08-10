# Publishing

Publishing consumes the resolved StagePlan rather than rediscovering files:

```xml
<Project Name="Gallery" Type="Application" Version="1.2.3">
  <Publish Name="portable">
    <Archive Format="zip" Output="dist/Gallery-${project.version}.zip" />
  </Publish>
  <Publish Name="windows">
    <Installer Format="msi" Output="dist/Gallery-${project.version}.msi" />
  </Publish>
</Project>
```

Each Publish definition has exactly one Folder, Archive, or Installer output.
Publish-only dependencies belong in its `<Dependencies>` child. The semantic
PublishPlan is backend-neutral; the currently implemented publisher maps it to
CPack where applicable.
