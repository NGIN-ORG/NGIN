---
title: Paths and filesystems
description: Separate lexical Path operations from real filesystem queries, symlink policy, and injected filesystem ownership.
---

# Paths and filesystems

`Path` is an owned lexical value. It combines/normalizes separators and exposes
filename, extension, parent, root, and relative operations without touching a
filesystem.

```cpp
NGIN::IO::Path base {"assets"};
auto image = base.Join("icons/app.png");
auto extension = image.Extension();
```

Lexical normalization cannot prove containment, existence, entry type, or
symlink destination. Use an `IFileSystem` operation with deliberate symlink
options for those questions.

## Filesystem implementations

- `LocalFileSystem` performs native local-disk operations.
- `VirtualFileSystem` resolves configured mount points and delegates to local
  or custom `IVirtualMount` implementations.
- `IFileSystem` is the injectable synchronous contract.
- `IAsyncFileSystem` is the coroutine-oriented contract.

All normal failures use `IOResult<T>`/`IOError`. Preserve portable code,
`systemCode`, primary/secondary paths, and message in diagnostics.

## Security boundary

For an untrusted relative path, rejecting `..` lexically is only one layer.
Canonicalization and symlink traversal can escape a root depending on options
and filesystem races. Define whether links may be followed and whether the
opened handle—not a pre-check string—is the authoritative containment boundary.
