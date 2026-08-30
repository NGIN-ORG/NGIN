---
title: Semaphore
description: Code reference for the NGIN.Sync counted-permit semaphore.
---

# `Semaphore<MaxCount>`

**Header:** `<NGIN/Sync/Semaphore.hpp>`  
**Namespace:** `NGIN::Sync`  
**Target:** `NGIN::Base::Foundation`  
**Defined:** [`Semaphore.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Sync/Semaphore.hpp#L9)

## Declaration

```cpp
template<int MaxCount = std::counting_semaphore<>::max()>
class Semaphore {
public:
    explicit Semaphore(int count = MaxCount) noexcept;

    void Lock() noexcept;
    bool TryLock() noexcept;
    void Unlock() noexcept;

    void lock() noexcept;
    bool try_lock() noexcept;
    void unlock() noexcept;
};
```

The initial count is the number of permits immediately available. Acquisition
blocks until and consumes a permit; `TryLock` returns immediately; release adds
one. The object is non-copyable.

## Preconditions and ownership

- The initial count and every release must keep the underlying count within
  `[0, MaxCount]`.
- A permit is not thread ownership; another thread may release according to the
  application protocol.
- The semaphore must outlive blocked/acquiring users.
- Destruction requires the owner to have stopped and joined/drained all users.

Because it satisfies BasicLockable, `LockGuard<Semaphore<...>>` can represent a
scope-bound permit.

