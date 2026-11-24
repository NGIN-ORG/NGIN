# Test NGIN

Choose the narrowest check that covers the change.

## CLI

```bash
cmake --build build/dev --target NGINCliTests
./build/dev/Tools/NGIN.CLI/tests/NGINCliTests
```

On Windows, use `NGINCliTests.exe`.

CI and CLI packaging also build every project kind in every supported layout,
compare the preview with the files written, and compile a separate consumer
of each library header. Run this check against a specific CLI binary with:

```bash
python3 Tools/NGIN.CLI/tests/test_scaffolds.py build/dev/Tools/NGIN.CLI/ngin
```

## Workspace flow

```bash
cmake --build build/dev --target ngin.workflow
ctest --test-dir build/dev --output-on-failure
```

## NGIN.Core

```bash
cmake -S Packages/NGIN.Core -B build/ngin-core-ci \
  -DNGIN_CORE_BUILD_TESTS=ON \
  -DNGIN_CORE_BUILD_EXAMPLES=OFF
cmake --build build/ngin-core-ci --config Release --target NGINCoreTests
ctest --test-dir build/ngin-core-ci --output-on-failure -C Release
```

Individual libraries document their own targets. Documentation-only changes do
not require a C++ build; check links, examples, terminology, and command names
instead.
