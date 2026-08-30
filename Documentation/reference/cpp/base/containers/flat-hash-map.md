---
title: FlatHashMap
description: Code reference for open-addressed map construction, lookup, insertion, removal, capacity, and invalidation.
---

# `FlatHashMap<Key, Value, Hash, KeyEqual, Alloc>`

**Header:** `<NGIN/Containers/FlatHashMap.hpp>`  
**Namespace:** `NGIN::Containers`  
**Defined:** [`FlatHashMap.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Containers/FlatHashMap.hpp#L77)

## Core operations

```cpp
void Insert(key, value);             // inserts or replaces equivalent key
void Remove(const key_type& key);    // no-op when absent
Value Get(const key_type&) const;    // copy; throws when absent
Value& GetRef(const key_type&);      // throws when absent
Value* GetPtr(const key_type&);      // nullptr when absent
bool Contains(const key_type&) const;
void Clear();
void Reserve(UIntSize entries);
void Rehash(UIntSize buckets);
UIntSize Size() const;
UIntSize Capacity() const;
```

Heterogeneous overloads participate when `Hash` and `KeyEqual` support the
alternate key in both equality directions.

## Type and failure contract

Allocation failure throws `std::bad_alloc`; missing `Get`/`GetRef` throws
`std::out_of_range`. Stored key/value types must satisfy the construction and
nothrow relocation requirements used by rehash/backward-shift deletion.

## Invalidation

Rehash/growth invalidates all iterators, pointers, and references. Any `Remove`
may backward-shift entries and invalidate more than the erased element. Insert
replaces the mapped value when the key exists and can grow otherwise.

