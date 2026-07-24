# NGIN.UI Source Compatibility and Deprecation Policy

NGIN.UI uses semantic versioning for its authored packages. The public source
surface is the installed headers under `NGIN/UI`, `NGIN/UI/Hosting`, and
`NGIN/UI/Backend/SDL3`.

## Version 0.x guarantees

- Patch releases within `0.1.x` preserve source compatibility for documented
  APIs and manifest features.
- A minor `0.x` release may make source-breaking changes only when the release
  notes provide a migration section and the change cannot reasonably be
  introduced through deprecation.
- Backend contract compatibility is negotiated separately through
  `BackendContractVersion`; a provider with the same major and a sufficient
  minor version remains loadable.
- NGIN.UI currently ships static libraries. It does not promise a stable C++
  binary ABI across compiler, standard-library, build-option, or package
  versions.
- Types and functions in `NGIN::UI::Testing` are supported test APIs but are
  not runtime ABI contracts.

The compile-time `NGIN_UI_VERSION_MAJOR`, `NGIN_UI_VERSION_MINOR`, and
`NGIN_UI_VERSION_PATCH` macros and matching namespace constants identify the
header version.

## Deprecation process

An API selected for removal is annotated with
`NGIN_UI_DEPRECATED("replacement and target release")`. Documentation and
release notes must state:

1. why it is deprecated;
2. the replacement;
3. the first release that warns;
4. the earliest release in which it may be removed.

A documented public API remains available for at least one subsequent minor
release after its first deprecation warning. Security, correctness, or
unimplementable platform contracts may require faster removal; such an
exception must be called out prominently in release notes.

Renames should provide a deprecated forwarding wrapper where that does not
preserve a flawed ownership, lifetime, or manifest contract. NGIN does not add
silent legacy manifest fallbacks: authored V4 migrations remain explicit.

## Compatibility checks

Release gates compile all public headers on Windows, Linux, and macOS, exercise
the backend contract tests, and build an external CMake consumer exclusively
from an install prefix. The API documentation coverage check prevents public
types from appearing without reference documentation.

The buildable
[`NGIN.UI.Gallery`](../../Examples/NGIN.UI.Gallery/) is the behavioral
compatibility catalogue. Its standalone and hosted products share the same view
implementation.
