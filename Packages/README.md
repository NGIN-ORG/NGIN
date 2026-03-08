# Packages

`Packages/` is the authoritative NGIN consumption layer in the umbrella workspace.

Each package wrapper defines:

- package identity and exposed version
- source binding metadata
- exposed artifacts such as libraries and executables
- runtime modules and plugins
- staged content
- optional backend build hints

Source code does not live here by default. Source-backed packages point at either:

- `Packages/NGIN.Core/` for first-class local platform code
- `Dependencies/NGIN/*` for first-party libraries developed externally
- `Dependencies/ThirdParty/*` for vendored third-party source trees
- a backend package name such as `SDL2` for imported CMake packages
