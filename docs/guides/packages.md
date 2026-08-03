# Packages

A `.nginpkg` gives a reusable dependency an identity and describes what it can
contribute: build targets, tools, generators, runtime files, features, and
compatibility information.

## Consume a package

Dependencies belong under the product's `<Uses>` section:

```xml
<Application>
  <Uses>
    <Package Name="NGIN.UI"
             Version=">=0.4.0 &lt;0.5.0"
             Scope="Target">
      <Feature Name="RuntimeAssets" />
    </Package>
  </Uses>
</Application>
```

Use `Tool` for a host-side tool dependency and `Runtime` for an optional runtime
such as `NGIN.Core`. Supported scopes are `Build`, `Target`, `Runtime`, `Test`,
`Dev`, and `Publish`.

## Find packages

Package sources come from the workspace and local user configuration. A local
source usually points at the repository's wrapper directory:

```xml
<Packages>
  <Source Name="local" Path="Packages" />
  <Version Name="NGIN.UI" Range=">=0.4.0 &lt;0.5.0" />
  <PackageProvider Name="NGIN.UI" Root="Packages/NGIN.UI" />
</Packages>
```

Provider entries connect source-built packages to the directory containing
their CMake project. The package wrapper decides how that build is integrated.

## CMake integration modes

- `AddSubdirectory` builds package source as part of the generated build.
- `FindPackage` consumes an installed CMake package.
- `Manual` uses wrapper-owned integration and is common for system tools.

## Restore and lock

```bash
ngin restore
ngin package list
ngin package show NGIN.UI
ngin package lock
ngin package verify-lock
```

`ngin.lock` records the selected package graph. Use `restore --locked` in CI
when resolution must agree with the lock file.

Package archives use `.nginpack`. They are package-store artifacts and are
separate from an application's published ZIP, TGZ, MSI, or DEB output.

See the [package manifest reference](../reference/package-manifest.md) and the
working wrappers under [`Packages/`](../../Packages).
