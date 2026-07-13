# Packages

`Packages/` is the package consumption layer for the umbrella workspace. Most
files here are package manifests or wrappers: they describe reusable identity and
what a package contributes to a resolved NGIN composition.

The important distinction is ownership:

- `Packages/*.nginpkg` files expose packages to NGIN tooling.
- Source code usually lives elsewhere.
- [`NGIN.Core`](NGIN.Core/) is the locally owned hosted runtime package and does
  contain source code.
- [`OpenSSL`](OpenSSL/) is a CMake `FindPackage` wrapper for the `OpenSSL::Crypto`
  target used by optional crypto provider integration; it declares a
  `CryptoProvider` feature for graph capability discovery and provider-package
  metadata for restore-capable workspace overrides, linkage support, and runtime
  artifact hints.
- [`libsodium`](libsodium/) is a CMake `FindPackage`/pkg-config wrapper that
  normalizes common libsodium installs to `libsodium::libsodium` for optional
  NGIN.Base crypto provider integration; it remains provider-manager-neutral
  until a workspace binds it to vcpkg, Conan, or another provider.
- [`BoringSSL`](BoringSSL/) is a CMake `FindPackage` wrapper that normalizes
  common BoringSSL libcrypto layouts to `BoringSSL::Crypto` for future optional
  crypto provider integration and declares the common `boringssl` provider
  package name plus libcrypto linkage/runtime metadata.
- [`NGIN.Tooling.ClangTidy`](NGIN.Tooling.ClangTidy/) exposes clang-tidy as a
  package-selected Analyze action.
- [`NGIN.Tooling.ClangFormat`](NGIN.Tooling.ClangFormat/) exposes clang-format
  as a package-selected Format action through the general stdout-transform
  adapter.
- First-party library source trees usually live under `../Dependencies/NGIN/`.
- Third-party source trees usually live under `../Dependencies/ThirdParty/`.

## What A Package Wrapper Describes

A package wrapper may declare:

- package name and version
- package dependencies
- build integration metadata
- exported artifacts such as libraries or executables
- runtime modules and plugins
- staged config, content, or other files
- optional backend build hints
- optional linkage and runtime deployment hints

Package wrappers describe reusable behavior. Workspace-local source ownership is
resolved through the workspace file and its `PackageSources` and
`PackageProviders`.

## CMake Integration Modes

For CMake-backed packages, wrappers may use:

- `Mode="AddSubdirectory"` for local or synced source trees
- `Mode="FindPackage"` for installed/exported CMake packages
- `Mode="Manual"` for handwritten compatibility wrappers
- `Mode="Generated"` when NGIN owns generated backend input

For the active package contract, see
[`../docs/specs/003-package-manifest-and-runtime-contributions.md`](../docs/specs/003-package-manifest-and-runtime-contributions.md).
For workspace package discovery and providers, see
[`../docs/specs/011-workspace-manifest.md`](../docs/specs/011-workspace-manifest.md).
