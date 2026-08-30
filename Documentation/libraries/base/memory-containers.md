---
title: Memory and containers
description: Choose explicit allocation strategies and allocator-aware containers.
---

# Memory and containers

The Memory and Containers areas make storage policy visible when applications
need more control than default allocation provides.

## Memory facilities

The subsystem includes allocator abstractions, arenas, allocation utilities,
and ownership helpers. Use an arena when many objects share a clear lifetime
and can be released together. Use a general allocator when allocations require
independent lifetimes.

## Container facilities

NGIN.Base containers are designed to cooperate with the library's allocation
model. Choose them when allocator control or a specific semantic contract is
required; otherwise a standard-library container may remain the clearest
choice.

## Questions to answer first

1. Who owns the storage?
2. Can any view outlive its backing allocation?
3. Is destruction individual or bulk?
4. Does the container move or invalidate elements during growth?
5. Is allocation failure part of the recoverable contract?

## Lifetime rule

A view, span, iterator, or reference never extends the lifetime of its backing
storage. Document the owner whenever a public API returns a borrowed value.
