---
title: Async I/O API
description: API reference for IAsyncFileSystem and type-erased asynchronous file and directory handles.
---

# Async I/O API

**Headers:** `<NGIN/IO/IAsyncFileSystem.hpp>`, `<NGIN/IO/AsyncFileHandle.hpp>`

`IAsyncFileSystem` returns `AsyncTask<T>`, the I/O task alias carrying
`IOError`. Its core virtual operations open files/directories, query metadata,
and copy using an explicit `NGIN::Async::TaskContext&`.

`AsyncFileHandle` is a copyable shared-state, type-erased handle. Its operation
table provides sequential `Read`/`Write`, offset `ReadAt`/`WriteAt`, `Flush`,
`Close`, and `IsOpen`. Each coroutine operation receives a `TaskContext` and
returns an I/O task; invalid handles complete with a typed invalid-handle error.

`AsyncDirectoryHandle` supplies the corresponding asynchronous directory
boundary.

The handle's shared state can outlive a wrapper copy, but spans supplied to a
read or write remain caller-owned until task completion. The task is cold under
NGIN.Async rules and makes progress only through its executor/runtime. Destroy
or reuse buffers only after the returned operation is terminal.

**Defined:** [`AsyncFileHandle.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/IO/AsyncFileHandle.hpp)
