# NGIN.Core

`NGIN.Core` is the optional hosted runtime package for NGIN applications. The
tooling layer can build and stage plain native C++ applications without it; use
`NGIN.Core` when an application wants a hosted startup model with services,
configuration, modules, lifecycle, reflection hooks, and diagnostics.

The smallest runnable examples are:

- [`../../Examples/App.HostedCore`](../../Examples/App.HostedCore/README.md) for
  code-first hosted startup
- [`../../Examples/App.Basic`](../../Examples/App.Basic/README.md) for
  manifest-owned runtime metadata

## Scope

`NGIN.Core` provides:

- kernel lifecycle orchestration
- module resolution and startup ordering
- service registry
- immediate and deferred event bus
- task runtime lanes
- layered configuration store
- static-first module loading with a path for later dynamic plugin integration

## Main Include

```cpp
#include <NGIN/Core/Core.hpp>
```

## Build and Test

From the repository root, the focused package verification flow is:

```bash
cmake -S Packages/NGIN.Core -B build/ngin-core-ci \
  -DNGIN_CORE_BUILD_TESTS=ON \
  -DNGIN_CORE_BUILD_EXAMPLES=OFF

cmake --build build/ngin-core-ci --config Release --target NGINCoreTests
ctest --test-dir build/ngin-core-ci --output-on-failure -C Release
```

For local package development from inside `Packages/NGIN.Core`, a simple build
tree also works:

```bash
cmake -S . -B build \
  -DNGIN_CORE_BUILD_TESTS=ON \
  -DNGIN_CORE_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Notes

- `NGIN.Core` depends on `NGIN.Base` and `NGIN.Log`.
- File access can be injected through `KernelHostConfig::fileSystem` or
  `ApplicationBuilder::UseFileSystem(...)`; `LocalFileSystem` is the default.
- `NGIN.Reflection` integration is optional.
- Runtime/tooling boundaries are defined in
  [`../../docs/specs/012-tooling-and-runtime-boundary.md`](../../docs/specs/012-tooling-and-runtime-boundary.md).
- Migration notes live in [`docs/Migration.md`](docs/Migration.md).
