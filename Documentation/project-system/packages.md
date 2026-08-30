---
title: Packages
description: Consume reusable libraries, tools, runtime assets, and build integrations through package requirements.
---

# Packages

Packages connect a product to reusable capabilities. A package can expose
libraries, tools, generated behavior, runtime artifacts, and transitive
requirements.

## Require a package

A project declares what it needs; the workspace and lock determine which
package instance satisfies that requirement.

```xml
<Executable Name="Game">
  <Uses>
    <Package Name="NGIN.ECS" Version="0.4" />
  </Uses>
  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
</Executable>
```

Use the schema emitted by the current CLI to confirm the exact accepted
attributes for the repository revision you are using:

```bash
ngin schema --format json
```

## Package roles

Packages may expose different roles:

- a linkable library;
- a host tool such as a generator, analyzer, or formatter;
- runtime files that must be staged;
- a plugin artifact and descriptor;
- build-provider metadata for a source-backed dependency.

A Tool or Plugin is an export role. It is not a project root kind.

## Restore and lock

```bash
ngin restore
ngin lock
```

Restore resolves package inputs. The dependency lock records the selected
instances and reproducibility identity. Do not bypass the lock in editor or
automation integrations.

## Package wrappers and sources

In the NGIN repository, `Packages/` owns wrapper and exposure metadata, while
`Dependencies/NGIN/` owns first-party library source trees. Change the wrapper
when integration is wrong; change the source tree when library behavior is
wrong.
