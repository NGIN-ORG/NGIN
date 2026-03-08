# NGIN.Core

NGIN.Core is the core hosting component for the NGIN platform.

## Scope

- Kernel lifecycle orchestration
- Module resolution and ordering
- Service registry
- Event bus (immediate + deferred)
- Task runtime lanes
- Layered configuration store
- Static-first module loading with dynamic-plugin seam for Spec 003

## Build

```bash
cmake -S . -B build \
  -DNGIN_CORE_BUILD_TESTS=ON \
  -DNGIN_CORE_BUILD_EXAMPLES=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Main API Include

```cpp
#include <NGIN/Core/Core.hpp>
```

## Notes

- Depends on `NGIN.Base` and `NGIN.Log`.
- `NGIN.Reflection` integration path is optional.
- Dynamic binary plugin loading is intentionally deferred to Spec 003.
- Breaking-change migration notes are in `docs/Migration.md`.
