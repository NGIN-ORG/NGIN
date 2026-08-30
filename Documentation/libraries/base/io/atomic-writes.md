---
title: Atomic writes and replacement
description: Update destination files through same-filesystem temporary writes, flush policy, and explicit rename/replace semantics.
---

# Atomic writes and replacement

Use `WriteAllTextAtomic`/`WriteAllBytesAtomic` when readers must see either the
old destination or a complete new destination—not a partially rewritten file.

```cpp
NGIN::IO::AtomicWriteOptions options;
auto result = NGIN::IO::WriteAllTextAtomic(
    fs, {"settings.json"}, serialized, options);
if (!result) {
    return Report(result.error());
}
```

The operation writes a temporary file in the destination context, applies its
flush/replacement policy, and replaces the destination using supported
same-filesystem semantics. “Atomic visibility” and “durable after power loss”
are separate guarantees; choose file and directory flush policy for the latter.

## Rename choices

- `Rename` uses replacement-oriented platform behavior.
- `RenameNoReplace` fails if the target exists; capability determines whether
  the no-replace guarantee is atomic.
- `ReplaceFile` targets same-filesystem atomic destination replacement.
- Cross-device rename returns `CrossDevice`; copy/delete is not atomic.

Preserve/restore metadata only where options and platform capabilities support
it. Failure cleanup of a temporary file is best-effort and should be included
in diagnostics/maintenance policy.

