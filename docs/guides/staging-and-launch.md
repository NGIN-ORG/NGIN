# Staging and running

Executable products have an implicit default Run. Customize it only when
needed:

```xml
<Run WorkingDirectory="content">
  <Argument>--development</Argument>
  <Environment Name="LOG_LEVEL" Value="debug" />
</Run>
<Stage>
  <File From="config/app.cfg" To="config/app.cfg" />
  <Directory From="content" To="content" />
</Stage>
```

`ngin stage` combines product artifacts with project and activated-package
contributions in one StagePlan. Collisions are errors. `ngin run` derives a
RunPlan from the same graph. Staging a Plugin makes the artifact available but
does not load it.
