# Profiles

A profile selects complete project behavior. It is more than a CMake build
type: optimization, symbols, platform, environment, dependencies, staging,
launch, tooling, and publishing can all change together.

```xml
<Project SchemaVersion="4" Name="Server" DefaultProfile="dev">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>

  <Profile Name="dev">
    <Defaults>
      <Optimization Mode="Off" />
      <DebugSymbols Enabled="true" />
      <LinkTimeOptimization Enabled="false" />
      <TargetPlatform Name="host" />
      <Environment Name="development" />
    </Defaults>
  </Profile>

  <Profile Name="shipping">
    <Defaults>
      <Optimization Mode="Speed" />
      <DebugSymbols Enabled="false" />
      <LinkTimeOptimization Enabled="true" />
      <TargetPlatform Name="linux-x64" />
      <Environment Name="production" />
    </Defaults>
    <Application>
      <Build>
        <Define Name="SERVER_SHIPPING" Value="1" />
      </Build>
      <Stage>
        <Config Source="config/production.json"
                Target="config/server.json"
                Collision="Override" />
      </Stage>
    </Application>
  </Profile>
</Project>
```

Optimization, debug symbols, and link-time optimization are independent.
NGIN derives the backend configuration instead of forcing profiles into the
usual `Debug` and `Release` combinations.

Select a profile explicitly with `--profile`. Otherwise the project default is
used, followed by an applicable workspace default.

```bash
ngin build --profile shipping
ngin diff --from-profile dev --to-profile shipping
```

Profile product sections are overlays. Named items merge by identity; explicit
removal and replacement are supported where the item contract defines them.
Use `ngin diff` and `ngin explain <kind>:<identity>` when an inherited value is
not obvious.
