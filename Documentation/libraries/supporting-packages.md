---
title: Supporting NGIN packages
description: Understand first-party tool, plugin, backend, accessibility, hosting, and diagnostics packages around the primary libraries.
---

# Supporting NGIN packages

Some capabilities belong in focused packages rather than the primary library
surface.

## Runtime and integration packages

| Package | Role |
| --- | --- |
| `NGIN.Diagnostics` | A staged diagnostics plugin with default configuration and an NGIN.Log dependency |
| `NGIN.UI.Backend.SDL3` | Provider for the `NGIN.UI.Backend` capability using SDL3 |
| `NGIN.UI.Accessibility.Windows` | Windows UI Automation integration for NGIN.UI semantics |
| `NGIN.UI.Hosting` | Lifecycle integration between NGIN.UI and NGIN.Core |

These packages keep platform and hosting dependencies outside portable core
APIs.

## Host tools

| Package | Exports |
| --- | --- |
| `NGIN.Reflection.MetaGen` | Reflection code-generation action and backing tool |
| `NGIN.Tooling.ClangTidy` | Explicit analyzer action |
| `NGIN.Tooling.ClangFormat` | Explicit formatter action |
| `NGIN.Benchmark` | `SyncProgram` and `BenchmarkTool` host tools |

Host tools execute on the build machine. Their output may contribute to a
target product, but the executable itself does not become a target runtime
dependency unless a separate package contract says so.

## External providers

Workspace wrappers also expose external libraries such as SDL2, SDL3, OpenSSL,
BoringSSL, and libsodium. Those packages provide composition metadata and
capabilities; their upstream projects remain third-party dependencies.

## Selection is semantic

Projects consume a named package export or capability. Workspace preferences
choose among compatible capability providers. Directory order and incidental
package discovery do not select a winner.
