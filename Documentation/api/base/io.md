---
title: NGIN.Base I/O and Process API
description: Paths, local and virtual filesystems, handles, whole-file helpers, async file I/O, processes, and dynamic libraries.
---

# NGIN.Base I/O and Process API

**Include:** `<NGIN/IO.hpp>`  
**Target:** `NGIN::Base::IO`  
**Namespace:** `NGIN::IO`

Start with `Path`, `LocalFileSystem`, and the whole-file helpers. Move to
handles or async I/O only when their control is useful.

## API map

| Need | Public API |
| --- | --- |
| Lexical path operations | `Path` |
| Local disk access | `LocalFileSystem`, `IFileSystem` |
| Fine-grained file I/O | `FileHandle`, `IFileHandle`, `FileView` |
| Directory-scoped access | `DirectoryHandle`, `IDirectoryHandle`, `DirectoryEnumerator` |
| Whole-file access | `ReadAllText`, `WriteAllText`, `ReadAllBytes`, `WriteAllBytes` |
| Crash-safe replacement | `WriteAllTextAtomic`, `WriteAllBytesAtomic`, `AtomicWriteOptions` |
| Mounted namespaces | `VirtualFileSystem` |
| Coroutine file operations | `FileSystemDriver`, `IAsyncFileSystem`, async handles |
| Child processes | `Process`, `ProcessOptions`, `ProcessResult`, `ProcessError` |
| Shared-library loading | `DynamicLibrary` |

## Results and errors

I/O operations return an `IOResult<T>` expected-like result. `IOError` contains:

```cpp
struct IOError {
    IOErrorCode code;
    Int32 systemCode;
    Path path;
    Path secondaryPath;
    String message;
};
```

Portable codes cover end-of-stream, invalid arguments, system errors,
unsupported operations, missing/existing paths, permissions, path errors,
wrong entry types, non-empty directories, would-block, cancellation, busy
resources, cross-device operations, and corrupt data. Preserve `systemCode`
and paths in diagnostics; the portable code alone often lacks the cause.

```cpp
auto text = NGIN::IO::ReadAllText(fs, {"config.json"});
if (!text) {
    const NGIN::IO::IOError& error = text.error();
    Report(error.code, error.systemCode, error.path);
    return;
}
Use(text.value());
```

## `Path` is lexical

`Path` normalizes and combines path text and exposes filename, extension,
parent, and relative-path operations. It does not touch a filesystem. Use
`GetInfo`, canonicalization, or filesystem operations to answer whether an
entry exists, what kind it is, or where a symlink resolves.

## Synchronous filesystem

```cpp
NGIN::IO::LocalFileSystem fs;

auto wrote = NGIN::IO::WriteAllTextAtomic(
    fs, {"settings.json"}, R"({"ready":true})");
if (!wrote) {
    return Report(wrote.error());
}
```

Use `IFileSystem` when code accepts an injected filesystem and
`LocalFileSystem` when it directly owns local-disk access.

Important semantics:

- `Rename` uses replacement-oriented platform rename behavior.
- `RenameNoReplace` fails if the destination exists. Check filesystem
  capabilities before depending on atomic no-replace behavior.
- Cross-device rename is not atomic and returns `CrossDevice`.
- `ReplaceFile` is for same-filesystem atomic destination replacement.
- Metadata and canonicalization can follow symlinks; pass explicit options
  when that changes your security boundary.

## Directory handles and enumeration

```cpp
auto directory = fs.OpenDirectory({"assets"});
if (!directory) return;

NGIN::IO::EnumerateOptions options;
options.populateInfo = true;
options.sortOrder = NGIN::IO::DirectorySortOrder::LexicalPath;

auto entries = fs.Enumerate({"assets"}, options);
```

A directory handle scopes relative operations to an opened directory. That
reduces repeated joins, but callers must still validate untrusted relative
paths according to their containment policy.

Enumeration order is unspecified unless `sortOrder` is set. With
`populateInfo == false`, name and path remain available while full metadata can
be absent. A cheap entry type can be `Unknown`.

## File handles

Use `FileHandle` for offset reads/writes, flushing, size changes, and explicit
close. It is an owning handle; views and borrowed buffers do not outlive the
handle or their storage. Handle-level methods can return partial transfers, so
loop until the required byte count or an error/end condition is reached.

## Async filesystem

Async operations require both a `TaskContext` and an explicitly owned driver.

```cpp
NGIN::IO::FileSystemDriver driver;
NGIN::Async::TaskContext context = driver.MakeTaskContext();
NGIN::IO::LocalFileSystem fileSystem;

auto task = NGIN::IO::ReadAllBytesAsync(fileSystem, context, path);
auto operation = NGIN::Async::Spawn(context, std::move(task));
```

Native support is platform-sensitive. Linux can use `io_uring`; Windows can
use IOCP for supported file operations. Path lookup and directory work may use
the driver's fallback path. Completion resumes on the caller's executor. The
driver and context must remain alive until work terminates.

For startup code and ordinary tools, synchronous access is usually simpler.

## Processes

`ProcessOptions` describes the executable, arguments, working directory,
environment, standard-stream behavior, and process-control options.
`Process::Run` returns a `ProcessResult` or `ProcessError`.

Do not build a shell command by concatenating untrusted strings. Pass an
executable and argument list through the process API so the platform adapter
owns quoting. Read stdout and stderr from their separate result fields when
configured for capture; a zero launch error does not imply a zero child exit
code.

## Dynamic libraries

`DynamicLibrary` owns a loaded native module and resolves symbols. A resolved
function or data pointer is valid only while the library stays loaded. Check
every load and symbol lookup result before casting or calling it. Native plugin
ABI compatibility is a separate requirement from successful symbol lookup.

## Common failures

| Symptom | Cause | Fix |
| --- | --- | --- |
| Path exists check gives the wrong security answer | Lexical `Path` used as filesystem truth | Query the filesystem with deliberate symlink options |
| Async task never completes | Driver or caller scheduler is not driven | Poll/run both owners until terminal completion |
| Truncated output | One handle operation transferred fewer bytes | Loop or use whole-file helpers |
| Rename returns `CrossDevice` | Source and destination are on different mounts | Copy, flush as needed, then remove source with explicit failure handling |

**Source:** [`NGIN/IO`](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/IO)
