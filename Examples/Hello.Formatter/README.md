# Hello.Formatter

`Hello.Formatter` is the focused edit-producing tool example. It opts into the
official `NGIN.Tooling.ClangFormat` package while the CLI and editor integration
remain independent of clang-format.

The source is intentionally unformatted so check and preview behavior is easy
to exercise.

```bash
./build/dev/Tools/NGIN.CLI/ngin format \
  --project Examples/Hello.Formatter/Hello.Formatter.nginproj \
  --profile Debug.Formatter \
  --output build/manual/Hello.Formatter
```

The command is a non-mutating check and exits with code `1` when changes are
needed. Add `--apply` to apply the proposed edits, or use `ngin tool run
cpp-format` to create a preview result without treating proposed changes as a
failure.

`clang-format` must be on `PATH`, or `NGIN_CLANG_FORMAT` must point to the
executable. The package is a system-tool wrapper and does not redistribute LLVM
binaries.
