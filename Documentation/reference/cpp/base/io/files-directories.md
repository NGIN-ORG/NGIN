---
title: File and directory handle API
description: API reference for move-only file handles, mapped views, byte readers, and directory enumeration.
---

# File and directory handle API

## File handles

**Headers:** `<NGIN/IO/FileHandle.hpp>`, `<NGIN/IO/IFileHandle.hpp>`

`FileHandle` is a move-only type-erased owner over `IFileHandle`. It exposes
sequential and offset read/write operations, flush, resize, size/position,
seek, close, and `IsOpen`/`IsValid`. Operations on an invalid wrapper return an
`IOError` rather than dereferencing missing state.

`FileView` is a move-only mapped-file view. `Data()` returns a borrowed
read-only byte span and `Size()` reports the mapped region. Closing or moving
the view invalidates previously obtained spans.

`IByteReader` is the minimal reader contract. `MemoryReader` adapts an existing
memory range and therefore borrows its source.

## Directories

**Headers:** `<NGIN/IO/DirectoryHandle.hpp>`, `<NGIN/IO/DirectoryEnumerator.hpp>`

`DirectoryHandle` owns a type-erased `IDirectoryHandle`. Enumeration produces
directory entries incrementally through `IDirectoryEnumerator`/
`DirectoryEnumerator`; entry metadata and traversal options are declared in
`FileSystemTypes.hpp`.

Handles are resource owners, not path aliases. Renaming or replacing a path
does not transfer an already-open handle. Preserve the handle for the complete
lifetime of its views and enumeration state.

[Browse file declarations](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/IO/FileHandle.hpp) and [directory declarations](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/IO/DirectoryHandle.hpp).
