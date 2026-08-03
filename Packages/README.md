# Packages

`Packages/` contains package manifests and locally owned package projects. A
wrapper tells the NGIN resolver what a dependency provides and how its source or
installed artifacts join a build.

## Main NGIN packages

| Package | Purpose |
| --- | --- |
| `NGIN.Base` | Foundational first-party library wrapper |
| `NGIN.Log` | Structured logging wrapper |
| `NGIN.Core` | Optional hosted application runtime |
| `NGIN.Reflection` | Reflection runtime wrapper |
| `NGIN.Reflection.MetaGen` | Reflection generator tool |
| `NGIN.ECS` | Entity-component-system wrapper |
| `NGIN.UI` | Backend-neutral UI toolkit |
| `NGIN.UI.Backend.SDL3` | SDL3 platform and renderer backend |
| `NGIN.UI.Accessibility.Windows` | Windows UI Automation provider |
| `NGIN.UI.Hosting` | `NGIN.Core` integration for UI applications |
| `NGIN.Tooling.ClangTidy` | Clang-Tidy action and driver |
| `NGIN.Tooling.ClangFormat` | Clang-Format action and driver |

Wrappers for third-party libraries describe integration; their upstream source
remains under `Dependencies/ThirdParty/`.

## What a wrapper owns

A `.nginpkg` can declare:

- identity, version, and compatibility;
- CMake integration mode and options;
- package dependencies;
- exported library targets, binaries, headers, or tools;
- optional features;
- generators, runtime files, and tool actions.

Source-backed CMake packages use `AddSubdirectory`. Installed packages use
`FindPackage`. `Manual` is available for wrappers that own a different
integration path.

Workspace `PackageProvider` entries map package names to source roots. This
keeps exposure and build policy in `Packages/` while implementation stays in
its owning source tree.

See [Using packages](../docs/guides/packages.md), the
[package manifest reference](../docs/reference/package-manifest.md), and the
existing `.nginpkg` files in this directory.
