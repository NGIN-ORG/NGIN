# Staging and launch

Projects stage their own files directly:

```xml
<Stage>
  <File Include="config/app.json" Into="config/app.json" />
  <Directory Include="assets" Into="assets" />
</Stage>
<Launch Name="default" Default="true">
  <Executable Product="App" />
  <WorkingDirectory Path="." />
  <Argument>--local</Argument>
  <Environment Name="LOG_LEVEL" Value="debug" />
</Launch>
```

Activated packages add required runtime files, notices, selected Assets, and
Plugin artifacts automatically. Stage destinations are relative, normalized,
and cannot escape the stage root. Secrets name an external source and are
resolved only at launch; they never enter the graph or generated files.
