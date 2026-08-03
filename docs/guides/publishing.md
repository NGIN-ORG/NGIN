# Publishing

Publishing turns a staged product into a folder, archive, or native installer.

```xml
<Project SchemaVersion="4" Name="Gallery" Version="1.2.3">
  <Application>
    <Publish Name="demo"
             Kind="Archive"
             Format="zip"
             Output="dist/Gallery-$(ProjectVersion).zip" />
  </Application>
</Project>
```

Run the named publish definition with:

```bash
ngin publish demo --profile Release
```

Supported output families are:

- `Folder`
- `Archive` using `zip` or `tgz`
- `Installer` using `msi` on Windows or `deb` on Linux

Installer publishing requires a semantic project version and explicit
installer identity, vendor, and contact metadata. Native formats also require
their platform tooling. WiX 7 requires the person or automation account doing
the build to accept its OSMF terms; NGIN does not accept legal terms on a
user's behalf.

Use `ngin graph --publish-plan --format json` to inspect the resolved publish
definition before producing output.
