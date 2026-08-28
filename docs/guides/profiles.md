# Profiles and selection

Profiles give a reusable name to selection facts:

```xml
<Profiles Default="dev">
  <Profile Name="dev" Configuration="Debug" />
  <Profile Name="release" Configuration="Release" />
</Profiles>
```

A Profile may select Configuration, Target, Toolchain, Run, and Option values.
Use `--profile <name>` with a project command. Explicit conflicting selection is
an error. Profile names never enter Composition Graph identity; expanded facts
do.

Debug, Release, host, and auto are built in and visible through
`ngin inspect --effective`. See the [workspace reference](../reference/workspace-manifest.md).
