---
title: Path and filesystem API
description: API reference for Path, filesystem contracts, implementations, options, and typed I/O errors.
---

# Path and filesystem API

## `Path`

**Header:** `<NGIN/IO/Path.hpp>`

`Path` owns normalized UTF-8 text. Queries include `IsEmpty`, `IsAbsolute`,
`IsRelative`, `Filename`, `Stem`, `Extension`, `Parent`, and `IsRoot`.
Transformations include `LexicallyNormal`, `LexicallyRelativeTo`, `Join`,
`FromNative`, and `ToNative`. `StartsWith` and `EndsWith` are lexical tests.
None of these access a filesystem.

## Filesystem interface

**Header:** `<NGIN/IO/IFileSystem.hpp>`

`IFileSystem` is the synchronous injectable contract. It reports
`FileSystemCapabilities` and exposes open, metadata, existence/type, directory,
copy/move/remove, canonicalization, and atomic-write operations through focused
options in `FileSystemTypes.hpp`.

`LocalFileSystem` implements native disk behavior. `VirtualFileSystem` resolves
mounts and delegates to local or custom virtual mounts. `FileSystemDriver`
provides the platform-facing implementation boundary.

## Errors and options

`IOResult<T>` is an expected result with `IOError`. The error preserves a
portable code, system code, message, and involved path values. Options make
open mode, creation, sharing, symlink following, replacement, and durability
choices explicit.

`AtomicWriteOptions` controls the temporary-write-and-replace helper. Atomic
visibility does not by itself guarantee durable persistence; request the
appropriate file and parent-directory flush policy.

**Defined:** [`Path.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/IO/Path.hpp)
