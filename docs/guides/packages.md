# Packages

Declare APIs your source uses directly. A package with one default Library is
concise:

```xml
<Dependencies><Package Name="NGIN.UI" Compatible="0.4" /></Dependencies>
```

Select a non-default or optional export by name:

```xml
<Package Name="OpenSSL">
  <Version AtLeast="3.2.0" Before="4.0.0" />
  <Use Library="TLS" />
</Package>
```

Package selection and export activation are separate. Libraries, Tools,
Actions, Plugins, and Assets are typed exports; public package configuration is
declared through typed Options. Required runtime files and notices are
automatic contributions, so consumers do not enable housekeeping Features.

Workspaces may manage constraints centrally with `<Version Name="..."
Compatible="..." />` and bind coordinates to PackageProviders. CMake target
names live only in the package's `cmake:` integration extension. See the
[package reference](../reference/package-manifest.md).
