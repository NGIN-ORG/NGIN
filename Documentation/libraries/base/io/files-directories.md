---
title: Files, directories, and enumeration
description: Use whole-file helpers or owning file/directory handles with explicit partial-transfer and metadata rules.
---

# Files, directories, and enumeration

Start with whole-file helpers for ordinary configuration/assets:

```cpp
NGIN::IO::LocalFileSystem fs;
auto text = NGIN::IO::ReadAllText(fs, {"settings.json"});
if (!text) {
    return Report(text.error());
}
```

Use `FileHandle`/`IFileHandle` for sequential/offset reads and writes, seek,
flush, size, and explicit close. Handles are move-only owners. A read/write may
transfer fewer bytes than requested; loop until the required count, EOF, or an
error.

`FileView` and caller spans borrow backing storage. Neither extends handle,
mapping, or buffer lifetime.

## Directory scope

`DirectoryHandle`/`IDirectoryHandle` perform relative operations from an opened
directory. This reduces repeated path joining but does not automatically make
untrusted traversal safe; apply the handle/backend’s containment contract.

`EnumerateOptions` controls recursion, metadata population, filtering, and
sort order. Order is unspecified unless requested. With metadata population
disabled, name/path remain available but `DirectoryEntry` type/details may be
unknown or absent.

`DirectoryEnumerator`/`IDirectoryEnumerator` expose incremental enumeration;
whole enumeration helpers collect results. Keep the owning directory/backend
state alive through iteration.

