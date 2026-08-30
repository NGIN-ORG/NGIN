---
title: Flat hash tables
description: Use FlatHashMap and FlatHashSet with explicit capacity, lookup, insertion, relocation, and key requirements.
---

# Flat hash tables

`FlatHashMap` stores bucket metadata, keys, and values in an open-addressed
allocation. This favors cache locality but makes relocation part of insertion,
erase, and rehash behavior.

## Core operations

```cpp
NGIN::Containers::FlatHashMap<std::string, int> counts;
counts.Reserve(100);
counts.Insert("red", 1);

if (int* count = counts.GetPtr("red")) {
    ++*count;
}

counts.Remove("red");
```

`Insert` adds the key or replaces the mapped value for an equivalent key.
`Contains` and `GetPtr` express absence without an exception/precondition.
`GetRef` and `Get` throw `std::out_of_range` when the key is absent; `Get`
returns a value copy.

Transparent hash/equality objects enable heterogeneous lookup overloads where
their callable contracts accept the alternate key type.

## Capacity and load

The map normalizes bucket counts to supported powers of two and grows before
exceeding its maximum load factor. `Reserve(n)` plans for entries; rehash changes
bucket storage even if logical entries are unchanged.

Key and value relocation requirements are part of the template contract. In
particular, backward-shift deletion relies on supported nothrow movement for
stored types. Check compiler diagnostics/reference when a type cannot meet the
requirements.

## Invalidation

Rehash invalidates all iterators/references/pointers into buckets. Erase
invalidates the erased entry and can relocate subsequent entries. Insert can
invalidate everything if it grows. Copy values or reacquire by key after a
mutation.
