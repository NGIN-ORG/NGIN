---
title: NGIN.IO API
description: Code-grounded index of paths, filesystems, handles, async I/O, processes, and dynamic libraries.
---

# NGIN.IO API

**Umbrella header:** `<NGIN/IO.hpp>`  
**Namespace:** `NGIN::IO`  
**Target:** `NGIN::Base::IO`

| Area | Central declarations | Reference |
| --- | --- | --- |
| Paths and filesystem model | `Path`, `IFileSystem`, `LocalFileSystem`, `VirtualFileSystem` | [Paths and filesystems](./io/paths-filesystems.md) |
| Handles and directories | `FileHandle`, `FileView`, `DirectoryHandle`, `DirectoryEnumerator` | [Files and directories](./io/files-directories.md) |
| Async I/O | `IAsyncFileSystem`, `AsyncFileHandle`, `AsyncDirectoryHandle` | [Async I/O](./io/async.md) |
| Processes and libraries | `Process`, `ProcessOptions`, `DynamicLibrary` | [Processes and libraries](./io/processes-libraries.md) |

Normal filesystem failures use `IOResult<T>`/`IOError`; process failures use
`ProcessExpected<T>`/`ProcessError`. Handles are move-only owners. Views,
enumerators, and async buffers preserve the lifetime of their owner or caller.

[Learn I/O from the beginning](../../../libraries/base/io.md).  
[Browse all IO headers](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/IO).
