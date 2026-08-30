---
title: BasicString and String
description: Code reference overview for allocator-aware owned code-unit strings and common aliases.
---

# `BasicString` and `String`

**Headers:** `<NGIN/Text/BasicString.hpp>`, `<NGIN/Text/String.hpp>`  
**Namespace:** `NGIN::Text`  
**Target:** `NGIN::Base::Foundation`

```cpp
template<
    class CharT,
    std::size_t SBOBytes,
    Memory::AllocatorConcept Alloc,
    class Growth,
    class Traits>
class BasicString;
```

The type owns a sequence of `CharT` code units, uses inline small-buffer
storage when possible, and grows through the stored allocator. `String.hpp`
provides common character/allocator aliases.

Central operations cover construction/assignment, append/insert/erase,
reserve/capacity, size/empty, views, data/C-string access, comparison, and
iteration. Exact overloads depend on the character and traits configuration.

## Contracts

- Size/capacity count code units, not Unicode scalar values or graphemes.
- Non-const mutation can invalidate views/pointers/iterators.
- Movement can preserve heap storage or move from inline storage; do not retain
  an address across movement.
- Borrowed allocator targets outlive the string and allocated capacity.
- Use `NGIN::Text::Unicode` for encoding validation/conversion/iteration.

[Browse `BasicString.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Text/BasicString.hpp).

