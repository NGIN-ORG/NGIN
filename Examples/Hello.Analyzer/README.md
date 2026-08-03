# Hello.Analyzer

A normal native application that selects the `NGIN.Tooling.ClangTidy` analyzer
feature. Its source intentionally contains one warning so terminal and editor
diagnostics are easy to verify.

```bash
ngin tool doctor --project Examples/Hello.Analyzer/Hello.Analyzer.nginproj --profile Debug.Analyzer
ngin analyze --project Examples/Hello.Analyzer/Hello.Analyzer.nginproj --profile Debug.Analyzer --output build/manual/Hello.Analyzer
```

`clang-tidy` must be on `PATH` or `NGIN_CLANG_TIDY` must point to it. The
package supplies integration, not LLVM binaries.

Read [`Hello.Analyzer.nginproj`](Hello.Analyzer.nginproj),
[`.clang-tidy`](.clang-tidy), and [`src/main.cpp`](src/main.cpp).
