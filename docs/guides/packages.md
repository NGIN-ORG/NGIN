# Consuming packages and capabilities

Put requirements under project Uses:

```xml
<Uses>
  <Package Name="NGIN.UI" Version="0.4" />
  <Capability Name="NGIN.UI.Backend" Version="1" />
</Uses>
```

Omitting children selects default exports. Select a non-default export with a
typed child such as `<Library Name="TLS" />` or `<Plugin Name="Diagnostics" />`.
`Version` is a compatibility request; Exact and bounded Version children are
available for stricter needs.

PackageProviders acquire exact instances. `ngin package lock` records those
results; the project manifest remains portable. Prefer capabilities when a
project needs a semantic contract rather than one implementation. See
[package-manifest.md](../reference/package-manifest.md).
