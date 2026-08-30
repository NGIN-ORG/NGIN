---
title: Mutexes and guards
description: Use exclusive and shared locks with scope-bound ownership and a deadlock-safe lock order.
---

# Mutexes and guards

## Scope-bound exclusive ownership

```cpp
class Catalog {
public:
    void Insert(Item item) {
        NGIN::Sync::LockGuard guard {mutex_};
        items_.push_back(std::move(item));
        ++version_;
    }

private:
    NGIN::Sync::Mutex mutex_;
    std::vector<Item> items_;
    std::uint64_t version_ = 0;
};
```

Both `items_` and `version_` form one invariant and are always accessed under
the same lock. `LockGuard` is movable but not copyable; moving transfers the
responsibility to unlock.

## Shared reads

```cpp
NGIN::Sync::SharedMutex mutex;

{
    NGIN::Sync::SharedLockGuard read {mutex};
    InspectSharedState();
}

{
    NGIN::Sync::LockGuard write {mutex};
    MutateSharedState();
}
```

Do not retain a pointer/reference into protected storage after the guard dies
unless another lifetime mechanism makes that access safe.

## Standard guards

NGIN mutexes satisfy standard lock concepts, so these are also valid:

```cpp
std::lock_guard lock {mutex};
std::shared_lock read {sharedMutex};
std::unique_lock deferred {mutex, std::defer_lock};
```

Use NGIN guards for the compact common case and standard guards when you need
deferred locking, condition-variable integration, or algorithms such as
`std::lock`.

## More than one lock

Assign a stable global order, such as object ID or ownership layer, and acquire
in that order everywhere. Do not call an unknown callback while holding a lock:
it may re-enter and acquire locks in an incompatible order.

When copying state between two objects, snapshot one under its lock, release
it, then apply under the other lock if the operation need not be atomic across
both. This often removes a two-lock cycle entirely.

