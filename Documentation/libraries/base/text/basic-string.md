---
title: BasicString storage and views
description: Use allocator-aware owned code units, small-buffer storage, aliases, views, and invalidation rules.
---

# `BasicString` storage and views

`BasicString<CharT, SBOBytes, Alloc, Growth, Traits>` owns contiguous code
units and maintains a trailing null element for C interoperability. The small
buffer is measured in bytes, while `Size()` is measured in `CharT` code units.

Common aliases are `String`, `UTF8String`, `UTF16String`, and `UTF32String`.
The UTF aliases state the code-unit type; they do not validate content.

```cpp
NGIN::Text::String name{"NGIN"};
name.Append(".Base");

auto prefix = name.Substr(0, 4);
```

Mutation, capacity growth, search, comparison, indexed access, and iteration
operate on code units. For UTF-8, one displayed character may occupy multiple
units and multiple code points may form one grapheme cluster.

`View()` and `AsBytes()` borrow the string's storage. Mutations that reallocate,
move, reset, or destroy the owner invalidate views, pointers, and iterators.
Use `UTF8FromBytes()` when byte-oriented UTF-8 input must become owned
`char8_t` storage.

Allocator and growth policy are template parameters. Passing a custom allocator
makes its lifetime/ownership part of the string contract. Use the normal aliases
unless a subsystem genuinely needs a different allocation policy.

Null termination supports APIs expecting a C string, but embedded null code
units remain possible; an external C API may observe only the prefix. Preserve
explicit lengths at binary/text boundaries.
