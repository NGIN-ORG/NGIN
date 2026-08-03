# Tooling and quality checks

NGIN tools are packages. A package describes the executable, a protocol driver,
and semantic actions such as analyze or format. Projects select features and
named runs; the CLI and VS Code extension consume the same resolved plan.

For example, enable the official Clang-Tidy wrapper:

```xml
<Application>
  <Uses>
    <Package Name="NGIN.Tooling.ClangTidy"
             Version=">=0.1.0 &lt;0.2.0"
             Scope="Dev">
      <Feature Name="Analyzer" />
    </Package>
  </Uses>
</Application>
```

The wrapper resolves `clang-tidy` from `NGIN_CLANG_TIDY` or `PATH`; it does not
ship LLVM binaries.

```bash
ngin tool list --available
ngin tool doctor
ngin tool plan
ngin analyze
ngin format --check
ngin format --apply
ngin quality
```

Drivers report diagnostics and proposed edits. NGIN owns gate policy, caching,
timeouts, result normalization, and edit application. Check and preview modes
do not modify source files; apply modes validate file digests before editing.

Use `ngin graph --tooling-plan --format json` to inspect the selected runs.
Package authors can continue with [Tool driver authoring](tool-driver-authoring.md)
and the [tool driver reference](../reference/tool-driver.md).
