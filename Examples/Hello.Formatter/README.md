# Hello.Formatter

This example selects the `NGIN.Tooling.ClangFormat` action. Its source is
intentionally unformatted so check, preview, and apply behavior are visible.

```bash
ngin package lock --project Examples/Hello.Formatter/Hello.Formatter.nginproj --output build/manual/Hello.Formatter.lock
ngin format --project Examples/Hello.Formatter/Hello.Formatter.nginproj --lock build/manual/Hello.Formatter.lock --output build/manual/Hello.Formatter
```

This Action writes clang-format's proposed output to the terminal and does not
silently modify authored files.

`clang-format` must be on `PATH`. The package supplies a trusted Tool/Action
contract and CMake binding, not LLVM binaries.
