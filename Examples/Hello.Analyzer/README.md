# Hello.Analyzer

`Hello.Analyzer` is the focused tool-execution example. It is a normal native
application that opts into the official `NGIN.Tooling.ClangTidy` package
feature.

## What This Proves

- Package-selected tool runs appear in the resolved graph.
- `ngin analyze` executes the package action through its declared driver.
- Tool configuration is read from the project-local `.clang-tidy` file.
- The source intentionally contains one `readability-magic-numbers` warning so
  editor integrations can show a Problems entry.

## Files To Read

- [`Hello.Analyzer.nginproj`](Hello.Analyzer.nginproj)
- [`.clang-tidy`](.clang-tidy)
- [`src/main.cpp`](src/main.cpp)

## Build And Run

From the repository root:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli

./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/Hello.Analyzer/Hello.Analyzer.nginproj \
  --profile Debug.Analyzer

./build/dev/Tools/NGIN.CLI/ngin graph \
  --project Examples/Hello.Analyzer/Hello.Analyzer.nginproj \
  --profile Debug.Analyzer \
  --tooling-plan

./build/dev/Tools/NGIN.CLI/ngin tool doctor \
  --project Examples/Hello.Analyzer/Hello.Analyzer.nginproj \
  --profile Debug.Analyzer

./build/dev/Tools/NGIN.CLI/ngin analyze \
  --project Examples/Hello.Analyzer/Hello.Analyzer.nginproj \
  --profile Debug.Analyzer \
  --output build/manual/Hello.Analyzer
```

Expected tooling plan:

```text
run cpp-static-analysis action=NGIN.Tooling.ClangTidy::analyze kind=Analyze state=ready
```

Expected diagnostic shape:

```text
[warning] .../Examples/Hello.Analyzer/src/main.cpp:...:...: 42 is a magic number [clang-tidy:readability-magic-numbers]
```

`ngin analyze` requires `clang-tidy` on `PATH` or `NGIN_CLANG_TIDY` pointing to
the executable. The package is a system-tool wrapper and does not redistribute
LLVM binaries.
