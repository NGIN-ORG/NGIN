# Hello.Formatter

This example selects the `NGIN.Tooling.ClangFormat` action. Its source is
intentionally unformatted so check, preview, and apply behavior are visible.

```bash
ngin format --project Examples/Hello.Formatter/Hello.Formatter.nginproj --profile Debug.Formatter --output build/manual/Hello.Formatter
```

The default check does not edit files and returns `1` when changes are needed.
Add `--apply` to apply digest-validated edits, or run `ngin tool run cpp-format`
to store a preview result.

`clang-format` must be on `PATH` or `NGIN_CLANG_FORMAT` must point to it. The
package supplies integration, not LLVM binaries.
