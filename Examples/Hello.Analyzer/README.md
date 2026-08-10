# Hello.Analyzer

A normal native application that explicitly selects the
`NGIN.Tooling.ClangTidy::Analyze` Action. Its source intentionally contains one
warning so terminal and editor
diagnostics are easy to verify.

```bash
ngin package lock --project Examples/Hello.Analyzer/Hello.Analyzer.nginproj --output build/manual/Hello.Analyzer.lock
ngin analyze --project Examples/Hello.Analyzer/Hello.Analyzer.nginproj --lock build/manual/Hello.Analyzer.lock --output build/manual/Hello.Analyzer
```

`clang-tidy` must be on `PATH`. The package supplies a trusted Tool/Action
contract and CMake binding, not LLVM binaries.

Read [`Hello.Analyzer.nginproj`](Hello.Analyzer.nginproj),
[`.clang-tidy`](.clang-tidy), and [`src/main.cpp`](src/main.cpp).
