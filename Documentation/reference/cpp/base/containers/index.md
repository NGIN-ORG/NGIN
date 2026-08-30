---
title: NGIN.Containers API reference
description: Symbol index for allocator-aware vectors, strings, flat hash maps, and concurrent hash maps.
---

# NGIN.Containers API reference

**Umbrella:** `<NGIN/Containers.hpp>`  
**Namespace:** `NGIN::Containers`  
**Target:** `NGIN::Base::Foundation`

| Symbol | Header | Reference |
| --- | --- | --- |
| [`Vector<T, Alloc>`](./vector.md) | `<NGIN/Containers/Vector.hpp>` | Contiguous growable sequence |
| [`BasicString<...>` / aliases](./string.md) | `<NGIN/Text/BasicString.hpp>`, `<NGIN/Text/String.hpp>` | Owned code-unit strings |
| [`FlatHashMap<K,V,...>`](./flat-hash-map.md) | `<NGIN/Containers/FlatHashMap.hpp>` | Open-addressed key/value table |
| [`ConcurrentHashMap<K,V,...>`](./concurrent-hash-map.md) | `<NGIN/Containers/ConcurrentHashMap.hpp>` | Sharded concurrent map with reclamation policy |

For usage and invalidation guidance, start with [Learn NGIN.Containers](../../../../libraries/base/containers.md).

